/* -*- Mode: c++; c-basic-offset: 2; tab-width: 4; indent-tabs-mode: nil; -*-
 * vim: set sw=2 ts=4 expandtab:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <atomic>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <math.h>
#include <queue>
#include <type_traits>
#include <unistd.h>

#include "AndroidBridge.h"
#include "AndroidBridgeUtilities.h"
#include "AndroidCompositorWidget.h"
#include "AndroidContentController.h"
#include "AndroidDragEvent.h"
#include "AndroidUiThread.h"
#include "AndroidView.h"
#include "AndroidWidgetUtils.h"
#include "gfxContext.h"
#include "GeckoEditableSupport.h"
#include "GeckoViewOutputStream.h"
#include "GeckoViewSupport.h"
#include "GLContext.h"
#include "GLContextProvider.h"
#include "JavaBuiltins.h"
#include "JavaExceptions.h"
#include "KeyEvent.h"
#include "MotionEvent.h"
#include "ScopedGLHelpers.h"
#include "ScreenHelperAndroid.h"
#include "TouchResampler.h"
#include "WidgetUtils.h"
#include "WindowRenderer.h"

#include "mozilla/EventForwards.h"
#include "nsAppShell.h"
#include "nsContentUtils.h"
#include "nsDragService.h"
#include "nsFocusManager.h"
#include "nsGkAtoms.h"
#include "nsGfxCIID.h"
#include "nsIDocShellTreeOwner.h"
#include "nsLayoutUtils.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "nsUserIdleService.h"
#include "nsViewManager.h"
#include "nsWidgetsCID.h"
#include "nsWindow.h"

#include "nsIWidgetListener.h"
#include "nsIWindowWatcher.h"
#include "nsIAppWindow.h"
#include "nsIPrintSettings.h"
#include "nsIPrintSettingsService.h"

#include "mozilla/Logging.h"
#include "mozilla/MiscEvents.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_android.h"
#include "mozilla/StaticPrefs_ui.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/WheelHandlingHelper.h"  // for WheelDeltaAdjustmentStrategy
#include "mozilla/a11y/SessionAccessibility.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/BrowserHost.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/Swizzle.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/ipc/Shmem.h"
#include "mozilla/java/EventDispatcherWrappers.h"
#include "mozilla/java/GeckoAppShellWrappers.h"
#include "mozilla/java/GeckoEditableChildWrappers.h"
#include "mozilla/java/GeckoResultWrappers.h"
#include "mozilla/java/GeckoSessionNatives.h"
#include "mozilla/java/GeckoSystemStateListenerWrappers.h"
#include "mozilla/java/PanZoomControllerNatives.h"
#include "mozilla/java/SessionAccessibilityWrappers.h"
#include "mozilla/java/SurfaceControlManagerWrappers.h"
#include "mozilla/jni/NativesInlines.h"
#include "mozilla/layers/APZEventState.h"
#include "mozilla/layers/APZInputBridge.h"
#include "mozilla/layers/APZThreadUtils.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/CompositorOGL.h"
#include "mozilla/layers/CompositorSession.h"
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/layers/UiCompositorControllerChild.h"
#include "mozilla/layers/IAPZCTreeManager.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/widget/AndroidVsync.h"
#include "mozilla/widget/Screen.h"

#define GVS_LOG(...) MOZ_LOG(sGVSupportLog, LogLevel::Warning, (__VA_ARGS__))

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::layers;
using namespace mozilla::widget;
using namespace mozilla::ipc;

using mozilla::dom::ContentChild;
using mozilla::dom::ContentParent;
using mozilla::gfx::DataSourceSurface;
using mozilla::gfx::IntSize;
using mozilla::gfx::Matrix;
using mozilla::gfx::SurfaceFormat;
using mozilla::java::GeckoSession;
using mozilla::java::sdk::IllegalStateException;
using GeckoPrintException = GeckoSession::GeckoPrintException;
static mozilla::LazyLogModule sGVSupportLog("GeckoViewSupport");

// All the toplevel windows that have been created; these are in
// stacking order, so the window at gTopLevelWindows[0] is the topmost
// one.
MOZ_RUNINIT static nsTArray<nsWindow*> gTopLevelWindows;

static bool sFailedToCreateGLContext = false;

// Multitouch swipe thresholds in inches
static const double SWIPE_MAX_PINCH_DELTA_INCHES = 0.4;
static const double SWIPE_MIN_DISTANCE_INCHES = 0.6;

static const double kTouchResampleVsyncAdjustMs = 5.0;

static const int32_t INPUT_RESULT_UNHANDLED =
    java::PanZoomController::INPUT_RESULT_UNHANDLED;
static const int32_t INPUT_RESULT_HANDLED =
    java::PanZoomController::INPUT_RESULT_HANDLED;
static const int32_t INPUT_RESULT_HANDLED_CONTENT =
    java::PanZoomController::INPUT_RESULT_HANDLED_CONTENT;
static const int32_t INPUT_RESULT_IGNORED =
    java::PanZoomController::INPUT_RESULT_IGNORED;

static const nsCString::size_type MAX_TOPLEVEL_DATA_URI_LEN = 2 * 1024 * 1024;

// Unique ID given to each widget, to identify it for the
// CompositorSurfaceManager.
static std::atomic<int32_t> sWidgetId{0};

namespace {
template <class Instance, class Impl>
std::enable_if_t<jni::detail::NativePtrPicker<Impl>::value ==
                     jni::detail::NativePtrType::REFPTR,
                 void>
CallAttachNative(Instance aInstance, Impl* aImpl) {
  Impl::AttachNative(aInstance, RefPtr<Impl>(aImpl).get());
}

template <class Instance, class Impl>
std::enable_if_t<jni::detail::NativePtrPicker<Impl>::value ==
                     jni::detail::NativePtrType::OWNING,
                 void>
CallAttachNative(Instance aInstance, Impl* aImpl) {
  Impl::AttachNative(aInstance, UniquePtr<Impl>(aImpl));
}

template <class Lambda>
bool DispatchToUiThread(const char* aName, Lambda&& aLambda) {
  if (RefPtr<nsThread> uiThread = GetAndroidUiThread()) {
    uiThread->Dispatch(NS_NewRunnableFunction(aName, std::move(aLambda)));
    return true;
  }
  return false;
}
}  // namespace

namespace mozilla {
namespace widget {

// For double click detection
static int64_t sLastMouseDownTime = 0;
static int32_t sLastMouseButtons = 0;
static int32_t sLastClickCount = 0;
static float sLastMouseDownX = 0;
static float sLastMouseDownY = 0;

using WindowPtr = jni::NativeWeakPtr<GeckoViewSupport>;

/**
 * PanZoomController handles its native calls on the UI thread, so make
 * it separate from GeckoViewSupport.
 */
class NPZCSupport final
    : public java::PanZoomController::NativeProvider::Natives<NPZCSupport> {
  WindowPtr mWindow;
  java::PanZoomController::NativeProvider::WeakRef mNPZC;

  // Stores the returnResult of each pending motion event between
  // HandleMotionEvent and FinishHandlingMotionEvent.
  std::queue<std::pair<uint64_t, java::GeckoResult::GlobalRef>>
      mPendingMotionEventReturnResults;

  RefPtr<AndroidVsync> mAndroidVsync;
  TouchResampler mTouchResampler;
  int mPreviousButtons = 0;
  bool mListeningToVsync = false;

  // Only true if mAndroidVsync is non-null and the resampling pref is set.
  bool mTouchResamplingEnabled = false;

  template <typename Lambda>
  class InputEvent final : public nsAppShell::Event {
    java::PanZoomController::NativeProvider::GlobalRef mNPZC;
    Lambda mLambda;

   public:
    InputEvent(const NPZCSupport* aNPZCSupport, Lambda&& aLambda)
        : mNPZC(aNPZCSupport->mNPZC), mLambda(std::move(aLambda)) {}

    void Run() override {
      MOZ_ASSERT(NS_IsMainThread());

      JNIEnv* const env = jni::GetGeckoThreadEnv();
      const auto npzcSupportWeak = GetNative(
          java::PanZoomController::NativeProvider::LocalRef(env, mNPZC));
      if (!npzcSupportWeak) {
        // We already shut down.
        env->ExceptionClear();
        return;
      }

      auto acc = npzcSupportWeak->Access();
      if (!acc) {
        // We already shut down.
        env->ExceptionClear();
        return;
      }

      auto win = acc->mWindow.Access();
      if (!win) {
        // We already shut down.
        env->ExceptionClear();
        return;
      }

      nsWindow* const window = win->GetNsWindow();
      if (!window) {
        // We already shut down.
        env->ExceptionClear();
        return;
      }

      window->UserActivity();
      return mLambda(window);
    }

    bool IsUIEvent() const override { return true; }
  };

  class MOZ_HEAP_CLASS Observer final : public AndroidVsync::Observer {
   public:
    static Observer* Create(jni::NativeWeakPtr<NPZCSupport>&& aNPZCSupport) {
      return new Observer(std::move(aNPZCSupport));
    }

   private:
    // Private constructor, part of a strategy to make sure
    // we're only able to create these on the heap.
    explicit Observer(jni::NativeWeakPtr<NPZCSupport>&& aNPZCSupport)
        : mNPZCSupport(std::move(aNPZCSupport)) {}

    void OnVsync(const TimeStamp& aTimeStamp) override {
      auto accessor = mNPZCSupport.Access();

      if (!accessor) {
        return;
      }

      accessor->mTouchResampler.NotifyFrame(
          aTimeStamp -
          TimeDuration::FromMilliseconds(kTouchResampleVsyncAdjustMs));
      accessor->ConsumeMotionEventsFromResampler();
    }

    void Dispose() override { delete this; }

    jni::NativeWeakPtr<NPZCSupport> mNPZCSupport;
  };

  Observer* mObserver = nullptr;

  template <typename Lambda>
  void PostInputEvent(Lambda&& aLambda) {
    // Use priority queue for input events.
    nsAppShell::PostEvent(
        MakeUnique<InputEvent<Lambda>>(this, std::move(aLambda)));
  }

 public:
  typedef java::PanZoomController::NativeProvider::Natives<NPZCSupport> Base;

  NPZCSupport(WindowPtr aWindow,
              const java::PanZoomController::NativeProvider::LocalRef& aNPZC)
      : mWindow(aWindow), mNPZC(aNPZC) {
#if defined(DEBUG)
    auto win(mWindow.Access());
    MOZ_ASSERT(!!win);
#endif  // defined(DEBUG)

    mAndroidVsync = AndroidVsync::GetInstance();
  }

  ~NPZCSupport() {
    if (mListeningToVsync) {
      MOZ_RELEASE_ASSERT(mAndroidVsync);
      mAndroidVsync->UnregisterObserver(mObserver, AndroidVsync::INPUT);
      mListeningToVsync = false;
    }
  }

  using Base::AttachNative;
  using Base::DisposeNative;

  void OnWeakNonIntrusiveDetach(already_AddRefed<Runnable> aDisposer) {
    RefPtr<Runnable> disposer = aDisposer;
    // There are several considerations when shutting down NPZC. 1) The
    // Gecko thread may destroy NPZC at any time when nsWindow closes. 2)
    // There may be pending events on the Gecko thread when NPZC is
    // destroyed. 3) mWindow may not be available when the pending event
    // runs. 4) The UI thread may destroy NPZC at any time when GeckoView
    // is destroyed. 5) The UI thread may destroy NPZC at the same time as
    // Gecko thread trying to destroy NPZC. 6) There may be pending calls
    // on the UI thread when NPZC is destroyed. 7) mWindow may have been
    // cleared on the Gecko thread when the pending call happens on the UI
    // thread.
    //
    // 1) happens through OnWeakNonIntrusiveDetach, which first notifies the UI
    // thread through Destroy; Destroy then calls DisposeNative, which
    // finally disposes the native instance back on the Gecko thread. Using
    // Destroy to indirectly call DisposeNative here also solves 5), by
    // making everything go through the UI thread, avoiding contention.
    //
    // 2) and 3) are solved by clearing mWindow, which signals to the
    // pending event that we had shut down. In that case the event bails
    // and does not touch mWindow.
    //
    // 4) happens through DisposeNative directly.
    //
    // 6) is solved by keeping a destroyed flag in the Java NPZC instance,
    // and only make a pending call if the destroyed flag is not set.
    //
    // 7) is solved by taking a lock whenever mWindow is modified on the
    // Gecko thread or accessed on the UI thread. That way, we don't
    // release mWindow until the UI thread is done using it, thus avoiding
    // the race condition.

    if (RefPtr<nsThread> uiThread = GetAndroidUiThread()) {
      auto npzc = java::PanZoomController::NativeProvider::GlobalRef(mNPZC);
      if (!npzc) {
        return;
      }

      uiThread->Dispatch(
          NS_NewRunnableFunction("NPZCSupport::OnWeakNonIntrusiveDetach",
                                 [npzc, disposer = std::move(disposer)] {
                                   npzc->SetAttached(false);
                                   disposer->Run();
                                 }));
    }
  }

  const java::PanZoomController::NativeProvider::Ref& GetJavaNPZC() const {
    return mNPZC;
  }

 public:
  void SetIsLongpressEnabled(bool aIsLongpressEnabled) {
    RefPtr<IAPZCTreeManager> controller;

    if (auto window = mWindow.Access()) {
      nsWindow* gkWindow = window->GetNsWindow();
      if (gkWindow) {
        controller = gkWindow->mAPZC;
      }
    }

    if (controller) {
      controller->SetLongTapEnabled(aIsLongpressEnabled);
    }
  }

  int32_t HandleScrollEvent(int64_t aTime, int32_t aMetaState, float aX,
                            float aY, float aHScroll, float aVScroll) {
    MOZ_ASSERT(AndroidBridge::IsJavaUiThread());

    RefPtr<IAPZCTreeManager> controller;

    if (auto window = mWindow.Access()) {
      nsWindow* gkWindow = window->GetNsWindow();
      if (gkWindow) {
        controller = gkWindow->mAPZC;
      }
    }

    if (!controller) {
      return INPUT_RESULT_UNHANDLED;
    }

    ScreenPoint origin = ScreenPoint(aX, aY);

    if (StaticPrefs::ui_scrolling_negate_wheel_scroll()) {
      aHScroll = -aHScroll;
      aVScroll = -aVScroll;
    }

    ScrollWheelInput input(
        nsWindow::GetEventTimeStamp(aTime), nsWindow::GetModifiers(aMetaState),
        ScrollWheelInput::SCROLLMODE_SMOOTH,
        ScrollWheelInput::SCROLLDELTA_PIXEL, origin, aHScroll, aVScroll, false,
        // XXX Do we need to support auto-dir scrolling
        // for Android widgets with a wheel device?
        // Currently, I just leave it unimplemented. If
        // we need to implement it, what's the extra work
        // to do?
        WheelDeltaAdjustmentStrategy::eNone);

    APZEventResult result = controller->InputBridge()->ReceiveInputEvent(input);
    if (result.GetStatus() == nsEventStatus_eConsumeNoDefault) {
      return INPUT_RESULT_IGNORED;
    }

    PostInputEvent([input = std::move(input), result](nsWindow* window) {
      WidgetWheelEvent wheelEvent = input.ToWidgetEvent(window);
      window->ProcessUntransformedAPZEvent(&wheelEvent, result);
    });

    switch (result.GetStatus()) {
      case nsEventStatus_eIgnore:
        return INPUT_RESULT_UNHANDLED;
      case nsEventStatus_eConsumeDoDefault:
        return result.GetHandledResult()->IsHandledByRoot()
                   ? INPUT_RESULT_HANDLED
                   : INPUT_RESULT_HANDLED_CONTENT;
      default:
        MOZ_ASSERT_UNREACHABLE("Unexpected nsEventStatus");
        return INPUT_RESULT_UNHANDLED;
    }
  }

 private:
  static MouseInput::ButtonType GetButtonType(int button) {
    MouseInput::ButtonType result = MouseInput::NONE;

    switch (button) {
      case java::sdk::MotionEvent::BUTTON_PRIMARY:
        result = MouseInput::PRIMARY_BUTTON;
        break;
      case java::sdk::MotionEvent::BUTTON_SECONDARY:
        result = MouseInput::SECONDARY_BUTTON;
        break;
      case java::sdk::MotionEvent::BUTTON_TERTIARY:
        result = MouseInput::MIDDLE_BUTTON;
        break;
      default:
        break;
    }

    return result;
  }

  static int16_t ConvertButtons(int buttons) {
    int16_t result = 0;

    if (buttons & java::sdk::MotionEvent::BUTTON_PRIMARY) {
      result |= MouseButtonsFlag::ePrimaryFlag;
    }
    if (buttons & java::sdk::MotionEvent::BUTTON_SECONDARY) {
      result |= MouseButtonsFlag::eSecondaryFlag;
    }
    if (buttons & java::sdk::MotionEvent::BUTTON_TERTIARY) {
      result |= MouseButtonsFlag::eMiddleFlag;
    }
    if (buttons & java::sdk::MotionEvent::BUTTON_BACK) {
      result |= MouseButtonsFlag::e4thFlag;
    }
    if (buttons & java::sdk::MotionEvent::BUTTON_FORWARD) {
      result |= MouseButtonsFlag::e5thFlag;
    }

    return result;
  }

  static int32_t ConvertAPZHandledPlace(APZHandledPlace aHandledPlace) {
    switch (aHandledPlace) {
      case APZHandledPlace::Unhandled:
        return INPUT_RESULT_UNHANDLED;
      case APZHandledPlace::HandledByRoot:
        return INPUT_RESULT_HANDLED;
      case APZHandledPlace::HandledByContent:
        return INPUT_RESULT_HANDLED_CONTENT;
      case APZHandledPlace::Invalid:
        MOZ_ASSERT_UNREACHABLE("The handled result should NOT be Invalid");
        return INPUT_RESULT_UNHANDLED;
    }
    MOZ_ASSERT_UNREACHABLE("Unknown handled result");
    return INPUT_RESULT_UNHANDLED;
  }

  static int32_t ConvertSideBits(SideBits aSideBits) {
    int32_t ret = java::PanZoomController::SCROLLABLE_FLAG_NONE;
    if (aSideBits & SideBits::eTop) {
      ret |= java::PanZoomController::SCROLLABLE_FLAG_TOP;
    }
    if (aSideBits & SideBits::eRight) {
      ret |= java::PanZoomController::SCROLLABLE_FLAG_RIGHT;
    }
    if (aSideBits & SideBits::eBottom) {
      ret |= java::PanZoomController::SCROLLABLE_FLAG_BOTTOM;
    }
    if (aSideBits & SideBits::eLeft) {
      ret |= java::PanZoomController::SCROLLABLE_FLAG_LEFT;
    }
    return ret;
  }

  static int32_t ConvertScrollDirections(
      layers::ScrollDirections aScrollDirections) {
    int32_t ret = java::PanZoomController::OVERSCROLL_FLAG_NONE;
    if (aScrollDirections.contains(layers::HorizontalScrollDirection)) {
      ret |= java::PanZoomController::OVERSCROLL_FLAG_HORIZONTAL;
    }
    if (aScrollDirections.contains(layers::VerticalScrollDirection)) {
      ret |= java::PanZoomController::OVERSCROLL_FLAG_VERTICAL;
    }
    return ret;
  }

  static java::PanZoomController::InputResultDetail::LocalRef
  ConvertAPZHandledResult(const APZHandledResult& aHandledResult) {
    return java::PanZoomController::InputResultDetail::New(
        ConvertAPZHandledPlace(aHandledResult.mPlace),
        ConvertSideBits(aHandledResult.mScrollableDirections),
        ConvertScrollDirections(aHandledResult.mOverscrollDirections));
  }

  static bool IsIntoDoubleClickThreshold(float aX, float aY) {
    int32_t deltaX = abs((int32_t)floorf(sLastMouseDownX - aX));
    int32_t deltaY = abs((int32_t)floorf(sLastMouseDownY - aY));
    int32_t threshold = StaticPrefs::widget_double_click_threshold();

    return (deltaX * deltaX + deltaY * deltaY < threshold * threshold);
  }

  static bool IsDoubleClick(int64_t aTime, float aX, float aY, int buttons) {
    if (sLastMouseButtons != buttons) {
      return false;
    }

    int64_t deltaTime = aTime - sLastMouseDownTime;
    if (deltaTime < (int64_t)StaticPrefs::widget_double_click_min() ||
        deltaTime > (int64_t)StaticPrefs::widget_double_click_timeout()) {
      return false;
    }

    return IsIntoDoubleClickThreshold(aX, aY);
  }

 public:
  int32_t HandleMouseEvent(int32_t aAction, int64_t aTime, int32_t aMetaState,
                           float aX, float aY, int buttons) {
    MOZ_ASSERT(AndroidBridge::IsJavaUiThread());

    RefPtr<IAPZCTreeManager> controller;

    if (auto window = mWindow.Access()) {
      nsWindow* gkWindow = window->GetNsWindow();
      if (gkWindow) {
        controller = gkWindow->mAPZC;
      }
    }

    if (!controller) {
      return INPUT_RESULT_UNHANDLED;
    }

    MouseInput::MouseType mouseType = MouseInput::MOUSE_NONE;
    MouseInput::ButtonType buttonType = MouseInput::NONE;
    switch (aAction) {
      case java::sdk::MotionEvent::ACTION_DOWN:
        mouseType = MouseInput::MOUSE_DOWN;
        buttonType = GetButtonType(buttons ^ mPreviousButtons);
        mPreviousButtons = buttons;

        if (IsDoubleClick(aTime, aX, aY, buttons)) {
          sLastClickCount++;
        } else {
          sLastClickCount = 1;
        }
        sLastMouseDownTime = aTime;
        sLastMouseDownX = aX;
        sLastMouseDownY = aY;
        sLastMouseButtons = buttons;
        break;
      case java::sdk::MotionEvent::ACTION_UP:
        mouseType = MouseInput::MOUSE_UP;
        buttonType = GetButtonType(buttons ^ mPreviousButtons);
        mPreviousButtons = buttons;
        break;
      case java::sdk::MotionEvent::ACTION_MOVE:
        mouseType = MouseInput::MOUSE_MOVE;

        if (!IsIntoDoubleClickThreshold(aX, aY)) {
          sLastClickCount = 0;
        }
        break;
      case java::sdk::MotionEvent::ACTION_HOVER_MOVE:
        mouseType = MouseInput::MOUSE_MOVE;
        break;
      case java::sdk::MotionEvent::ACTION_HOVER_ENTER:
        mouseType = MouseInput::MOUSE_WIDGET_ENTER;
        break;
      case java::sdk::MotionEvent::ACTION_HOVER_EXIT:
        mouseType = MouseInput::MOUSE_WIDGET_EXIT;
        break;
      default:
        break;
    }

    if (mouseType == MouseInput::MOUSE_NONE) {
      return INPUT_RESULT_UNHANDLED;
    }

    ScreenPoint origin = ScreenPoint(aX, aY);

    MouseInput input(
        mouseType, buttonType, MouseEvent_Binding::MOZ_SOURCE_MOUSE,
        ConvertButtons(buttons), origin, nsWindow::GetEventTimeStamp(aTime),
        nsWindow::GetModifiers(aMetaState));

    APZEventResult result = controller->InputBridge()->ReceiveInputEvent(input);
    if (result.GetStatus() == nsEventStatus_eConsumeNoDefault) {
      return INPUT_RESULT_IGNORED;
    }

    PostInputEvent([input = std::move(input), result,
                    clickCount = sLastClickCount](nsWindow* window) {
      WidgetMouseEvent mouseEvent =
          input.ToWidgetEvent<WidgetMouseEvent>(window);
      mouseEvent.mClickCount = clickCount;
      window->ProcessUntransformedAPZEvent(&mouseEvent, result);
      if (MouseInput::SECONDARY_BUTTON == input.mButtonType) {
        if ((StaticPrefs::ui_context_menus_after_mouseup() &&
             MouseInput::MOUSE_UP == input.mType) ||
            (!StaticPrefs::ui_context_menus_after_mouseup() &&
             MouseInput::MOUSE_DOWN == input.mType)) {
          MouseInput contextMenu = input;

          // Actually we don't dispatch context menu event to APZ since we don't
          // handle it on APZ yet. If handling it, we need to consider how to
          // dispatch it on APZ thread. It may cause a race condition.
          contextMenu.mType = MouseInput::MOUSE_CONTEXTMENU;

          if (contextMenu.IsPointerEventType()) {
            WidgetPointerEvent contextMenuEvent =
                contextMenu.ToWidgetEvent<WidgetPointerEvent>(window);
            window->ProcessUntransformedAPZEvent(&contextMenuEvent, result);
          } else {
            WidgetMouseEvent contextMenuEvent =
                contextMenu.ToWidgetEvent<WidgetMouseEvent>(window);
            window->ProcessUntransformedAPZEvent(&contextMenuEvent, result);
          }
        }
      }
    });

    switch (result.GetStatus()) {
      case nsEventStatus_eIgnore:
        return INPUT_RESULT_UNHANDLED;
      case nsEventStatus_eConsumeDoDefault:
        return result.GetHandledResult()->IsHandledByRoot()
                   ? INPUT_RESULT_HANDLED
                   : INPUT_RESULT_HANDLED_CONTENT;
      default:
        MOZ_ASSERT_UNREACHABLE("Unexpected nsEventStatus");
        return INPUT_RESULT_UNHANDLED;
    }
  }

  // Convert MotionEvent touch radius and orientation into the format required
  // by w3c touchevents.
  // toolMajor and toolMinor span a rectangle that's oriented as per
  // aOrientation, centered around the touch point.
  static std::pair<float, ScreenSize> ConvertOrientationAndRadius(
      float aOrientation, float aToolMajor, float aToolMinor) {
    float angle = aOrientation * 180.0f / M_PI;
    // w3c touchevents spec does not allow orientations == 90
    // this shifts it to -90, which will be shifted to zero below
    if (angle >= 90.0) {
      angle -= 180.0f;
    }

    // w3c touchevent radii are given with an orientation between 0 and
    // 90. The radii are found by removing the orientation and
    // measuring the x and y radii of the resulting ellipse. For
    // Android orientations >= 0 and < 90, use the y radius as the
    // major radius, and x as the minor radius. However, for an
    // orientation < 0, we have to shift the orientation by adding 90,
    // and reverse which radius is major and minor.
    ScreenSize radius;
    if (angle < 0.0f) {
      angle += 90.0f;
      radius =
          ScreenSize(int32_t(aToolMajor / 2.0f), int32_t(aToolMinor / 2.0f));
    } else {
      radius =
          ScreenSize(int32_t(aToolMinor / 2.0f), int32_t(aToolMajor / 2.0f));
    }

    return std::make_pair(angle, radius);
  }

  static void SetTiltXY(float aOrientation, float aTilt,
                        SingleTouchData& aSingleTouchData) {
    float r = sinf(aTilt);
    float z = cosf(aTilt);

    float x = atan2f(sinf(-aOrientation) * r, z);
    float y = atan2f(cosf(-aOrientation) * r, z);

    aSingleTouchData.mTiltX = int32_t(floorf(x * 180.0 / M_PI));
    aSingleTouchData.mTiltY = int32_t(floorf(y * 180.0 / M_PI));
  }

  void HandleMotionEvent(
      const java::PanZoomController::NativeProvider::LocalRef& aInstance,
      jni::Object::Param aEventData, float aScreenX, float aScreenY,
      jni::Object::Param aResult) {
    MOZ_ASSERT(AndroidBridge::IsJavaUiThread());

    auto returnResult = java::GeckoResult::Ref::From(aResult);
    auto eventData =
        java::PanZoomController::MotionEventData::Ref::From(aEventData);
    nsTArray<int32_t> pointerId(eventData->PointerId()->GetElements());
    size_t pointerCount = pointerId.Length();
    MultiTouchInput::MultiTouchType type;
    size_t startIndex = 0;
    size_t endIndex = pointerCount;

    switch (eventData->Action()) {
      case java::sdk::MotionEvent::ACTION_DOWN:
      case java::sdk::MotionEvent::ACTION_POINTER_DOWN:
        type = MultiTouchInput::MULTITOUCH_START;
        break;
      case java::sdk::MotionEvent::ACTION_MOVE:
        type = MultiTouchInput::MULTITOUCH_MOVE;
        break;
      case java::sdk::MotionEvent::ACTION_UP:
      case java::sdk::MotionEvent::ACTION_POINTER_UP:
        // for pointer-up events we only want the data from
        // the one pointer that went up
        type = MultiTouchInput::MULTITOUCH_END;
        startIndex = eventData->ActionIndex();
        endIndex = startIndex + 1;
        break;
      case java::sdk::MotionEvent::ACTION_OUTSIDE:
      case java::sdk::MotionEvent::ACTION_CANCEL:
        type = MultiTouchInput::MULTITOUCH_CANCEL;
        break;
      default:
        if (returnResult) {
          returnResult->Complete(
              java::sdk::Integer::ValueOf(INPUT_RESULT_UNHANDLED));
        }
        return;
    }

    MultiTouchInput input(type, eventData->Time(),
                          nsWindow::GetEventTimeStamp(eventData->Time()), 0);
    input.modifiers = nsWindow::GetModifiers(eventData->MetaState());
    input.mTouches.SetCapacity(endIndex - startIndex);
    input.mScreenOffset =
        ExternalIntPoint(int32_t(floorf(aScreenX)), int32_t(floorf(aScreenY)));
    switch (eventData->ToolType()) {
      case java::sdk::MotionEvent::TOOL_TYPE_STYLUS:
        input.mInputSource = MouseEvent_Binding::MOZ_SOURCE_PEN;
        break;
      default:
        input.mInputSource = MouseEvent_Binding::MOZ_SOURCE_TOUCH;
        break;
    }

    size_t historySize = eventData->HistorySize();
    nsTArray<int64_t> historicalTime(
        eventData->HistoricalTime()->GetElements());
    MOZ_RELEASE_ASSERT(historicalTime.Length() == historySize);

    // Each of these is |historySize| sets of |pointerCount| values.
    size_t historicalDataCount = historySize * pointerCount;
    nsTArray<float> historicalX(eventData->HistoricalX()->GetElements());
    nsTArray<float> historicalY(eventData->HistoricalY()->GetElements());
    nsTArray<float> historicalOrientation(
        eventData->HistoricalOrientation()->GetElements());
    nsTArray<float> historicalPressure(
        eventData->HistoricalPressure()->GetElements());
    nsTArray<float> historicalToolMajor(
        eventData->HistoricalToolMajor()->GetElements());
    nsTArray<float> historicalToolMinor(
        eventData->HistoricalToolMinor()->GetElements());

    MOZ_RELEASE_ASSERT(historicalX.Length() == historicalDataCount);
    MOZ_RELEASE_ASSERT(historicalY.Length() == historicalDataCount);
    MOZ_RELEASE_ASSERT(historicalOrientation.Length() == historicalDataCount);
    MOZ_RELEASE_ASSERT(historicalPressure.Length() == historicalDataCount);
    MOZ_RELEASE_ASSERT(historicalToolMajor.Length() == historicalDataCount);
    MOZ_RELEASE_ASSERT(historicalToolMinor.Length() == historicalDataCount);

    // Each of these is |pointerCount| values.
    nsTArray<float> x(eventData->X()->GetElements());
    nsTArray<float> y(eventData->Y()->GetElements());
    nsTArray<float> orientation(eventData->Orientation()->GetElements());
    nsTArray<float> pressure(eventData->Pressure()->GetElements());
    nsTArray<float> tilt(eventData->Tilt()->GetElements());
    nsTArray<float> toolMajor(eventData->ToolMajor()->GetElements());
    nsTArray<float> toolMinor(eventData->ToolMinor()->GetElements());

    MOZ_ASSERT(x.Length() == pointerCount);
    MOZ_ASSERT(y.Length() == pointerCount);
    MOZ_ASSERT(orientation.Length() == pointerCount);
    MOZ_ASSERT(pressure.Length() == pointerCount);
    MOZ_ASSERT(tilt.Length() == pointerCount);
    MOZ_ASSERT(toolMajor.Length() == pointerCount);
    MOZ_ASSERT(toolMinor.Length() == pointerCount);

    for (size_t i = startIndex; i < endIndex; i++) {
      auto [orien, radius] = ConvertOrientationAndRadius(
          orientation[i], toolMajor[i], toolMinor[i]);

      ScreenIntPoint point(int32_t(floorf(x[i])), int32_t(floorf(y[i])));
      SingleTouchData singleTouchData(pointerId[i], point, radius, orien,
                                      pressure[i]);
      SetTiltXY(orientation[i], tilt[i], singleTouchData);

      for (size_t historyIndex = 0; historyIndex < historySize;
           historyIndex++) {
        size_t historicalI = historyIndex * pointerCount + i;
        auto [historicalAngle, historicalRadius] = ConvertOrientationAndRadius(
            historicalOrientation[historicalI],
            historicalToolMajor[historicalI], historicalToolMinor[historicalI]);
        ScreenIntPoint historicalPoint(
            int32_t(floorf(historicalX[historicalI])),
            int32_t(floorf(historicalY[historicalI])));
        singleTouchData.mHistoricalData.AppendElement(
            SingleTouchData::HistoricalTouchData{
                nsWindow::GetEventTimeStamp(historicalTime[historyIndex]),
                historicalPoint,
                {},  // mLocalScreenPoint will be computed later by APZ
                historicalRadius,
                historicalAngle,
                historicalPressure[historicalI]});
      }

      input.mTouches.AppendElement(singleTouchData);
    }

    if (mAndroidVsync &&
        eventData->Action() == java::sdk::MotionEvent::ACTION_DOWN) {
      // Query pref value at the beginning of a touch gesture so that we don't
      // leave events stuck in the resampler after a pref flip.
      mTouchResamplingEnabled = StaticPrefs::android_touch_resampling_enabled();
    }

    if (!mTouchResamplingEnabled) {
      FinishHandlingMotionEvent(std::move(input),
                                java::GeckoResult::LocalRef(returnResult));
      return;
    }

    uint64_t eventId = mTouchResampler.ProcessEvent(std::move(input));
    mPendingMotionEventReturnResults.push(
        {eventId, java::GeckoResult::GlobalRef(returnResult)});

    RegisterOrUnregisterForVsync(mTouchResampler.InTouchingState());
    ConsumeMotionEventsFromResampler();
  }

  void RegisterOrUnregisterForVsync(bool aNeedVsync) {
    MOZ_RELEASE_ASSERT(mAndroidVsync);
    if (aNeedVsync && !mListeningToVsync) {
      MOZ_ASSERT(!mObserver);
      auto win = mWindow.Access();
      if (!win) {
        return;
      }
      RefPtr<nsWindow> gkWindow = win->GetNsWindow();
      if (!gkWindow) {
        return;
      }
      MutexAutoLock lock(gkWindow->GetDestroyMutex());
      if (gkWindow->Destroyed()) {
        return;
      }
      jni::NativeWeakPtr<NPZCSupport> weakPtrToThis =
          gkWindow->GetNPZCSupportWeakPtr();
      mObserver = Observer::Create(std::move(weakPtrToThis));
      mAndroidVsync->RegisterObserver(mObserver, AndroidVsync::INPUT);
    } else if (!aNeedVsync && mListeningToVsync) {
      mAndroidVsync->UnregisterObserver(mObserver, AndroidVsync::INPUT);
      mObserver = nullptr;
    }
    mListeningToVsync = aNeedVsync;
  }

  void HandleDragEvent(int32_t aAction, int64_t aTime, float aX, float aY,
                       jni::Object::Param aDropData) {
    MOZ_ASSERT(AndroidBridge::IsJavaUiThread());

    RefPtr<IAPZCTreeManager> controller;
    if (auto window = mWindow.Access()) {
      if (nsWindow* gkWindow = window->GetNsWindow()) {
        controller = gkWindow->mAPZC;
      }
    }

    if (!controller) {
      return;
    }

    MouseInput::MouseType mouseType = MouseInput::MouseType::MOUSE_NONE;
    switch (aAction) {
      case java::sdk::DragEvent::ACTION_DRAG_STARTED:
        mouseType = MouseInput::MouseType::MOUSE_DRAG_START;
        break;
      case java::sdk::DragEvent::ACTION_DRAG_ENDED:
        mouseType = MouseInput::MouseType::MOUSE_DRAG_END;
        break;
      case java::sdk::DragEvent::ACTION_DRAG_ENTERED:
        mouseType = MouseInput::MouseType::MOUSE_DRAG_ENTER;
        break;
      case java::sdk::DragEvent::ACTION_DRAG_LOCATION:
        mouseType = MouseInput::MouseType::MOUSE_DRAG_OVER;
        break;
      case java::sdk::DragEvent::ACTION_DRAG_EXITED:
        mouseType = MouseInput::MouseType::MOUSE_DRAG_EXIT;
        break;
      case java::sdk::DragEvent::ACTION_DROP:
        mouseType = MouseInput::MouseType::MOUSE_DROP;
        break;
      default:
        break;
    }
    ScreenPoint origin = ScreenPoint(aX, aY);
    MouseInput input(
        mouseType, MouseInput::NONE, MouseEvent_Binding::MOZ_SOURCE_MOUSE, 0,
        origin, nsWindow::GetEventTimeStamp(aTime), nsWindow::GetModifiers(0));

    APZEventResult result = controller->InputBridge()->ReceiveInputEvent(input);
    if (result.GetStatus() == nsEventStatus_eConsumeNoDefault) {
      return;
    }

    PostInputEvent(
        [input = std::move(input), result, aAction, aX, aY,
         dropData = jni::Object::GlobalRef(aDropData)](nsWindow* window) {
          window->OnDragEvent(aAction, aX, aY, dropData, result, input);
        });
  }

  void ConsumeMotionEventsFromResampler() {
    auto outgoing = mTouchResampler.ConsumeOutgoingEvents();
    while (!outgoing.empty()) {
      auto outgoingEvent = std::move(outgoing.front());
      outgoing.pop();
      java::GeckoResult::GlobalRef returnResult;
      if (outgoingEvent.mEventId) {
        // Look up the GeckoResult for this event.
        // The outgoing events from the resampler are in the same order as the
        // original events, and no event IDs are skipped.
        MOZ_RELEASE_ASSERT(!mPendingMotionEventReturnResults.empty());
        auto pair = mPendingMotionEventReturnResults.front();
        mPendingMotionEventReturnResults.pop();
        MOZ_RELEASE_ASSERT(pair.first == *outgoingEvent.mEventId);
        returnResult = pair.second;
      }
      FinishHandlingMotionEvent(std::move(outgoingEvent.mEvent),
                                java::GeckoResult::LocalRef(returnResult));
    }
  }

  void FinishHandlingMotionEvent(MultiTouchInput&& aInput,
                                 java::GeckoResult::LocalRef&& aReturnResult) {
    RefPtr<IAPZCTreeManager> controller;

    if (auto window = mWindow.Access()) {
      nsWindow* gkWindow = window->GetNsWindow();
      if (gkWindow) {
        controller = gkWindow->mAPZC;
      }
    }

    if (!controller) {
      if (aReturnResult) {
        aReturnResult->Complete(java::PanZoomController::InputResultDetail::New(
            INPUT_RESULT_UNHANDLED,
            java::PanZoomController::SCROLLABLE_FLAG_NONE,
            java::PanZoomController::OVERSCROLL_FLAG_NONE));
      }
      return;
    }

    APZInputBridge::InputBlockCallback callback;
    if (aReturnResult) {
      callback = [aReturnResult = java::GeckoResult::GlobalRef(aReturnResult)](
                     uint64_t aInputBlockId,
                     const APZHandledResult& aHandledResult) {
        aReturnResult->Complete(ConvertAPZHandledResult(aHandledResult));
      };
    }
    APZEventResult result = controller->InputBridge()->ReceiveInputEvent(
        aInput, std::move(callback));

    if (result.GetStatus() == nsEventStatus_eConsumeNoDefault) {
      if (aReturnResult) {
        if (result.GetHandledResult() != Nothing()) {
          aReturnResult->Complete(
              ConvertAPZHandledResult(result.GetHandledResult().value()));
        } else {
          MOZ_ASSERT_UNREACHABLE(
              "nsEventStatus_eConsumeNoDefault should involve a valid "
              "APZHandledResult");
          aReturnResult->Complete(
              java::PanZoomController::InputResultDetail::New(
                  INPUT_RESULT_IGNORED,
                  java::PanZoomController::SCROLLABLE_FLAG_NONE,
                  java::PanZoomController::OVERSCROLL_FLAG_NONE));
        }
      }
      return;
    }

    // Dispatch APZ input event on Gecko thread.
    PostInputEvent([input = std::move(aInput), result](nsWindow* window) {
      WidgetTouchEvent touchEvent = input.ToWidgetEvent(window);
      window->ProcessUntransformedAPZEvent(&touchEvent, result);
      window->DispatchHitTest(touchEvent);
    });

    if (aReturnResult && result.GetHandledResult() != Nothing()) {
      MOZ_ASSERT(result.GetStatus() == nsEventStatus_eConsumeDoDefault ||
                 result.GetStatus() == nsEventStatus_eIgnore);
      aReturnResult->Complete(
          ConvertAPZHandledResult(result.GetHandledResult().value()));
    }
  }
};

NS_IMPL_ISUPPORTS(AndroidView, nsIGeckoViewEventDispatcher, nsIGeckoViewView)

nsresult AndroidView::GetInitData(JSContext* aCx,
                                  JS::MutableHandle<JS::Value> aOut) {
  if (!mInitData) {
    aOut.setNull();
    return NS_OK;
  }

  return widget::EventDispatcher::UnboxBundle(aCx, mInitData, aOut);
}

/**
 * Compositor has some unique requirements for its native calls, so make it
 * separate from GeckoViewSupport.
 */
class LayerViewSupport final
    : public GeckoSession::Compositor::Natives<LayerViewSupport> {
  WindowPtr mWindow;
  GeckoSession::Compositor::WeakRef mCompositor;
  Atomic<bool, ReleaseAcquire> mCompositorPaused;
  java::sdk::Surface::GlobalRef mSurface;
  java::sdk::SurfaceControl::GlobalRef mSurfaceControl;
  int32_t mX;
  int32_t mY;
  int32_t mWidth;
  int32_t mHeight;
  // Used to communicate with the gecko compositor from the UI thread.
  // Set in NotifyCompositorCreated and cleared in
  // NotifyCompositorSessionLost.
  RefPtr<UiCompositorControllerChild> mUiCompositorControllerChild;
  // Whether we have requested a new Surface from the GeckoSession.
  bool mRequestedNewSurface = false;

  Maybe<uint32_t> mDefaultClearColor;

  struct CaptureRequest {
    explicit CaptureRequest() : mResult(nullptr) {}
    explicit CaptureRequest(java::GeckoResult::GlobalRef aResult,
                            java::sdk::Bitmap::GlobalRef aBitmap,
                            const ScreenRect& aSource,
                            const IntSize& aOutputSize)
        : mResult(aResult),
          mBitmap(aBitmap),
          mSource(aSource),
          mOutputSize(aOutputSize) {}

    // where to send the pixels
    java::GeckoResult::GlobalRef mResult;

    // where to store the pixels
    java::sdk::Bitmap::GlobalRef mBitmap;

    ScreenRect mSource;

    IntSize mOutputSize;
  };
  std::queue<CaptureRequest> mCapturePixelsResults;

  // In order to use Event::HasSameTypeAs in PostTo(), we cannot make
  // LayerViewEvent a template because each template instantiation is
  // a different type. So implement LayerViewEvent as a ProxyEvent.
  class LayerViewEvent final : public nsAppShell::ProxyEvent {
    using Event = nsAppShell::Event;

   public:
    static UniquePtr<Event> MakeEvent(UniquePtr<Event>&& event) {
      return MakeUnique<LayerViewEvent>(std::move(event));
    }

    explicit LayerViewEvent(UniquePtr<Event>&& event)
        : nsAppShell::ProxyEvent(std::move(event)) {}

    void PostTo(LinkedList<Event>& queue) override {
      // Give priority to compositor events, but keep in order with
      // existing compositor events.
      nsAppShell::Event* event = queue.getFirst();
      while (event && event->HasSameTypeAs(this)) {
        event = event->getNext();
      }
      if (event) {
        event->setPrevious(this);
      } else {
        queue.insertBack(this);
      }
    }
  };

 public:
  typedef GeckoSession::Compositor::Natives<LayerViewSupport> Base;

  LayerViewSupport(WindowPtr aWindow,
                   const GeckoSession::Compositor::LocalRef& aInstance)
      : mWindow(aWindow), mCompositor(aInstance), mCompositorPaused(true) {
#if defined(DEBUG)
    auto win(mWindow.Access());
    MOZ_ASSERT(!!win);
#endif  // defined(DEBUG)
  }

  ~LayerViewSupport() {}

  using Base::AttachNative;
  using Base::DisposeNative;

  void OnWeakNonIntrusiveDetach(already_AddRefed<Runnable> aDisposer) {
    RefPtr<Runnable> disposer = aDisposer;
    if (RefPtr<nsThread> uiThread = GetAndroidUiThread()) {
      GeckoSession::Compositor::GlobalRef compositor(mCompositor);
      if (!compositor) {
        return;
      }

      uiThread->Dispatch(NS_NewRunnableFunction(
          "LayerViewSupport::OnWeakNonIntrusiveDetach",
          [compositor, disposer = std::move(disposer),
           results = &mCapturePixelsResults, window = mWindow]() mutable {
            if (auto accWindow = window.Access()) {
              while (!results->empty()) {
                auto aResult =
                    java::GeckoResult::LocalRef(results->front().mResult);
                if (aResult) {
                  aResult->CompleteExceptionally(
                      java::sdk::IllegalStateException::New(
                          "The compositor has detached from the session")
                          .Cast<jni::Throwable>());
                }
                results->pop();
              }
            }

            compositor->OnCompositorDetached();
            disposer->Run();
          }));
    }
  }

  const GeckoSession::Compositor::Ref& GetJavaCompositor() const {
    return mCompositor;
  }

  bool CompositorPaused() const { return mCompositorPaused; }

  /// Called from the main thread whenever the compositor has been
  /// (re)initialized.
  void NotifyCompositorCreated(
      RefPtr<UiCompositorControllerChild> aUiCompositorControllerChild) {
    MOZ_ASSERT(AndroidBridge::IsJavaUiThread());
    mUiCompositorControllerChild = aUiCompositorControllerChild;

    if (mDefaultClearColor) {
      mUiCompositorControllerChild->SetDefaultClearColor(*mDefaultClearColor);
    }

    if (!mCompositorPaused) {
      // If we are using SurfaceControl but mSurface is null, that means the
      // previous surface was destroyed along with the the previous
      // compositor, and we need to create a new one.
      if (mSurfaceControl && !mSurface) {
        mSurface = java::SurfaceControlManager::GetInstance()->GetChildSurface(
            mSurfaceControl, mWidth, mHeight);
      }

      if (auto window{mWindow.Access()}) {
        nsWindow* gkWindow = window->GetNsWindow();
        if (gkWindow) {
          mUiCompositorControllerChild->OnCompositorSurfaceChanged(
              gkWindow->mWidgetId, mSurface);
        }
      }

      bool resumed = mUiCompositorControllerChild->ResumeAndResize(
          mX, mY, mWidth, mHeight);
      if (!resumed) {
        gfxCriticalNote
            << "Failed to resume compositor from NotifyCompositorCreated";
        RequestNewSurface();
      }
    }
  }

  /// Called from the main thread whenever the compositor has been destroyed.
  void NotifyCompositorSessionLost() {
    MOZ_ASSERT(AndroidBridge::IsJavaUiThread());
    mUiCompositorControllerChild = nullptr;

    if (mSurfaceControl) {
      // If we are using SurfaceControl then we must set the Surface to null
      // here to ensure we create a new one when the new compositor is
      // created.
      mSurface = nullptr;
    }

    if (auto window = mWindow.Access()) {
      while (!mCapturePixelsResults.empty()) {
        auto result =
            java::GeckoResult::LocalRef(mCapturePixelsResults.front().mResult);
        if (result) {
          result->CompleteExceptionally(
              java::sdk::IllegalStateException::New(
                  "Compositor session lost during screen pixels request")
                  .Cast<jni::Throwable>());
        }
        mCapturePixelsResults.pop();
      }
    }
  }

  java::sdk::Surface::Param GetSurface() { return mSurface; }

 private:
  already_AddRefed<DataSourceSurface> FlipScreenPixels(
      Shmem& aMem, const ScreenIntSize& aInSize, const ScreenRect& aInRegion,
      const IntSize& aOutSize) {
    RefPtr<gfx::DataSourceSurface> image =
        gfx::Factory::CreateWrappingDataSourceSurface(
            aMem.get<uint8_t>(),
            StrideForFormatAndWidth(SurfaceFormat::B8G8R8A8, aInSize.width),
            IntSize(aInSize.width, aInSize.height), SurfaceFormat::B8G8R8A8);
    RefPtr<gfx::DrawTarget> drawTarget =
        gfxPlatform::GetPlatform()->CreateOffscreenContentDrawTarget(
            aOutSize, SurfaceFormat::B8G8R8A8);
    if (!drawTarget) {
      return nullptr;
    }

    drawTarget->SetTransform(Matrix::Scaling(1.0, -1.0) *
                             Matrix::Translation(0, aOutSize.height));

    gfx::Rect srcRect(aInRegion.x,
                      (aInSize.height - aInRegion.height) - aInRegion.y,
                      aInRegion.width, aInRegion.height);
    gfx::Rect destRect(0, 0, aOutSize.width, aOutSize.height);
    drawTarget->DrawSurface(image, destRect, srcRect);

    RefPtr<gfx::SourceSurface> snapshot = drawTarget->Snapshot();
    RefPtr<gfx::DataSourceSurface> data = snapshot->GetDataSurface();
    return data.forget();
  }

  /**
   * Compositor methods
   */
 public:
  void AttachNPZC(jni::Object::Param aNPZC) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aNPZC);

    auto locked(mWindow.Access());
    if (!locked) {
      return;  // Already shut down.
    }

    nsWindow* gkWindow = locked->GetNsWindow();

    // We can have this situation if we get two GeckoViewSupport::Transfer()
    // called before the first AttachNPZC() gets here. Just detach the current
    // instance since that's what happens in GeckoViewSupport::Transfer() as
    // well.
    gkWindow->mNPZCSupport.Detach();

    auto npzc = java::PanZoomController::NativeProvider::LocalRef(
        jni::GetGeckoThreadEnv(),
        java::PanZoomController::NativeProvider::Ref::From(aNPZC));
    gkWindow->mNPZCSupport =
        jni::NativeWeakPtrHolder<NPZCSupport>::Attach(npzc, mWindow, npzc);

    DispatchToUiThread(
        "LayerViewSupport::AttachNPZC",
        [npzc = java::PanZoomController::NativeProvider::GlobalRef(npzc)] {
          npzc->SetAttached(true);
        });
  }

  void OnBoundsChanged(int32_t aLeft, int32_t aTop, int32_t aWidth,
                       int32_t aHeight) {
    MOZ_ASSERT(NS_IsMainThread());
    auto acc = mWindow.Access();
    if (!acc) {
      return;  // Already shut down.
    }

    nsWindow* gkWindow = acc->GetNsWindow();
    if (!gkWindow) {
      return;
    }

    gkWindow->Resize(aLeft, aTop, aWidth, aHeight, /* repaint */ false);
  }

  void NotifyMemoryPressure() {
    MOZ_ASSERT(NS_IsMainThread());
    auto acc = mWindow.Access();
    if (!acc) {
      return;  // Already shut down.
    }

    nsWindow* gkWindow = acc->GetNsWindow();
    if (!gkWindow || !gkWindow->mCompositorBridgeChild) {
      return;
    }

    gkWindow->mCompositorBridgeChild->SendNotifyMemoryPressure();
  }

  void SetDynamicToolbarMaxHeight(int32_t aHeight) {
    MOZ_ASSERT(NS_IsMainThread());
    auto acc = mWindow.Access();
    if (!acc) {
      return;  // Already shut down.
    }

    nsWindow* gkWindow = acc->GetNsWindow();
    if (!gkWindow) {
      return;
    }

    gkWindow->UpdateDynamicToolbarMaxHeight(ScreenIntCoord(aHeight));
  }

  void OnPipModeChanged(bool aPipMode) {
    MOZ_ASSERT(NS_IsMainThread());
    auto win(mWindow.Access());
    if (!win) {
      return;  // Already shut down.
    }

    nsWindow* gkWindow = win->GetNsWindow();
    if (!gkWindow) {
      return;
    }

    gkWindow->PipModeChanged(aPipMode);
  }

  void OnKeyboardHeightChanged(int32_t aHeight) {
    MOZ_ASSERT(NS_IsMainThread());
    auto win(mWindow.Access());
    if (!win) {
      return;  // Already shut down.
    }

    nsWindow* gkWindow = win->GetNsWindow();
    if (!gkWindow) {
      return;
    }

    gkWindow->KeyboardHeightChanged(ScreenIntCoord(aHeight));
  }

  void SyncPauseCompositor() {
    MOZ_ASSERT(AndroidBridge::IsJavaUiThread());

    // Set this true prior to attempting to pause the compositor, so that if
    // pausing fails the subsequent recovery knows to initialize the compositor
    // in a paused state.
    mCompositorPaused = true;

    if (mUiCompositorControllerChild) {
      mUiCompositorControllerChild->Pause();

      mSurface = nullptr;
      mSurfaceControl = nullptr;
      if (auto window = mWindow.Access()) {
        nsWindow* gkWindow = window->GetNsWindow();
        if (gkWindow) {
          mUiCompositorControllerChild->OnCompositorSurfaceChanged(
              gkWindow->mWidgetId, nullptr);
        }
      }
    }

    if (auto lock{mWindow.Access()}) {
      while (!mCapturePixelsResults.empty()) {
        auto result =
            java::GeckoResult::LocalRef(mCapturePixelsResults.front().mResult);
        if (result) {
          result->CompleteExceptionally(
              java::sdk::IllegalStateException::New(
                  "The compositor has detached from the session")
                  .Cast<jni::Throwable>());
        }
        mCapturePixelsResults.pop();
      }
    }
  }

  void SyncResumeCompositor() {
    MOZ_ASSERT(AndroidBridge::IsJavaUiThread());

    // Set this false prior to attempting to resume the compositor, so that if
    // resumption fails the subsequent recovery knows to initialize the
    // compositor in a resumed state.
    mCompositorPaused = false;

    if (mUiCompositorControllerChild) {
      bool resumed = mUiCompositorControllerChild->Resume();
      if (!resumed) {
        gfxCriticalNote
            << "Failed to resume compositor from SyncResumeCompositor";
        RequestNewSurface();
      }
    }
  }

  void SyncResumeResizeCompositor(
      const GeckoSession::Compositor::LocalRef& aObj, int32_t aX, int32_t aY,
      int32_t aWidth, int32_t aHeight, jni::Object::Param aSurface,
      jni::Object::Param aSurfaceControl) {
    MOZ_ASSERT(AndroidBridge::IsJavaUiThread());

    // Set this false prior to attempting to resume the compositor, so that if
    // resumption fails the subsequent recovery knows to initialize the
    // compositor in a resumed state.
    mCompositorPaused = false;

    mX = aX;
    mY = aY;
    mWidth = aWidth;
    mHeight = aHeight;
    if (StaticPrefs::widget_android_use_surfacecontrol_AtStartup()) {
      mSurfaceControl =
          java::sdk::SurfaceControl::GlobalRef::From(aSurfaceControl);
    }
    if (mSurfaceControl) {
      // When using SurfaceControl, we create a child Surface to render in to
      // rather than rendering directly in to the Surface provided by the
      // application. This allows us to work around a bug on some versions of
      // Android when recovering from a GPU process crash.
      mSurface = java::SurfaceControlManager::GetInstance()->GetChildSurface(
          mSurfaceControl, mWidth, mHeight);
    } else {
      mSurface = java::sdk::Surface::GlobalRef::From(aSurface);
    }

    if (mUiCompositorControllerChild) {
      if (auto window = mWindow.Access()) {
        nsWindow* gkWindow = window->GetNsWindow();
        if (gkWindow) {
          // Send new Surface to GPU process, if one exists.
          mUiCompositorControllerChild->OnCompositorSurfaceChanged(
              gkWindow->mWidgetId, mSurface);
        }
      }

      bool resumed = mUiCompositorControllerChild->ResumeAndResize(
          aX, aY, aWidth, aHeight);
      if (!resumed) {
        gfxCriticalNote
            << "Failed to resume compositor from SyncResumeResizeCompositor";
        // Only request a new Surface if this SyncResumeAndResize call is not
        // response to a previous request, otherwise we will get stuck in an
        // infinite loop.
        if (!mRequestedNewSurface) {
          RequestNewSurface();
        }
        return;
      }
    }

    mRequestedNewSurface = false;

    class OnResumedEvent : public nsAppShell::Event {
      GeckoSession::Compositor::GlobalRef mCompositor;

     public:
      explicit OnResumedEvent(GeckoSession::Compositor::GlobalRef&& aCompositor)
          : mCompositor(std::move(aCompositor)) {}

      void Run() override {
        MOZ_ASSERT(NS_IsMainThread());

        JNIEnv* const env = jni::GetGeckoThreadEnv();
        const auto lvsHolder =
            GetNative(GeckoSession::Compositor::LocalRef(env, mCompositor));

        if (!lvsHolder) {
          env->ExceptionClear();
          return;  // Already shut down.
        }

        auto lvs(lvsHolder->Access());
        if (!lvs) {
          env->ExceptionClear();
          return;  // Already shut down.
        }

        auto win = lvs->mWindow.Access();
        if (!win) {
          env->ExceptionClear();
          return;  // Already shut down.
        }

        // When we get here, the compositor has already been told to
        // resume. This means it's now safe for layer updates to occur.
        // Since we might have prevented one or more draw events from
        // occurring while the compositor was paused, we need to
        // schedule a draw event now.
        if (!lvs->mCompositorPaused) {
          nsWindow* const gkWindow = win->GetNsWindow();
          if (gkWindow) {
            gkWindow->RedrawAll();
          }
        }
      }
    };

    // Use priority queue for timing-sensitive event.
    nsAppShell::PostEvent(
        MakeUnique<LayerViewEvent>(MakeUnique<OnResumedEvent>(aObj)));
  }

  void RequestNewSurface() {
    if (const auto& compositor = GetJavaCompositor()) {
      mRequestedNewSurface = true;
      if (mSurfaceControl) {
        java::SurfaceControlManager::GetInstance()->RemoveSurface(
            mSurfaceControl);
      }
      compositor->RequestNewSurface();
    }
  }

  mozilla::jni::Object::LocalRef GetMagnifiableSurface() {
    return mozilla::jni::Object::LocalRef::From(GetSurface());
  }

  void SyncInvalidateAndScheduleComposite() {
    if (!mUiCompositorControllerChild) {
      return;
    }

    if (AndroidBridge::IsJavaUiThread()) {
      mUiCompositorControllerChild->InvalidateAndRender();
      return;
    }

    if (RefPtr<nsThread> uiThread = GetAndroidUiThread()) {
      uiThread->Dispatch(NewRunnableMethod<>(
                             "LayerViewSupport::InvalidateAndRender",
                             mUiCompositorControllerChild,
                             &UiCompositorControllerChild::InvalidateAndRender),
                         nsIThread::DISPATCH_NORMAL);
    }
  }

  void SetMaxToolbarHeight(int32_t aHeight) {
    MOZ_ASSERT(AndroidBridge::IsJavaUiThread());

    if (mUiCompositorControllerChild) {
      mUiCompositorControllerChild->SetMaxToolbarHeight(aHeight);
    }
  }

  void SetFixedBottomOffset(int32_t aOffset) {
    if (auto acc{mWindow.Access()}) {
      nsWindow* gkWindow = acc->GetNsWindow();
      if (gkWindow) {
        gkWindow->UpdateDynamicToolbarOffset(ScreenIntCoord(aOffset));
      }
    }

    if (RefPtr<nsThread> uiThread = GetAndroidUiThread()) {
      uiThread->Dispatch(NS_NewRunnableFunction(
          "LayerViewSupport::SetFixedBottomOffset", [this, offset = aOffset] {
            if (mUiCompositorControllerChild) {
              mUiCompositorControllerChild->SetFixedBottomOffset(offset);
            }
          }));
    }
  }

  void SendToolbarAnimatorMessage(int32_t aMessage) {
    if (!mUiCompositorControllerChild) {
      return;
    }

    if (AndroidBridge::IsJavaUiThread()) {
      mUiCompositorControllerChild->ToolbarAnimatorMessageFromUI(aMessage);
      return;
    }

    if (RefPtr<nsThread> uiThread = GetAndroidUiThread()) {
      uiThread->Dispatch(
          NewRunnableMethod<int32_t>(
              "LayerViewSupport::ToolbarAnimatorMessageFromUI",
              mUiCompositorControllerChild,
              &UiCompositorControllerChild::ToolbarAnimatorMessageFromUI,
              aMessage),
          nsIThread::DISPATCH_NORMAL);
    }
  }

  void RecvToolbarAnimatorMessage(int32_t aMessage) {
    auto compositor = GeckoSession::Compositor::LocalRef(mCompositor);
    if (compositor) {
      compositor->RecvToolbarAnimatorMessage(aMessage);
    }
  }

  void SetDefaultClearColor(int32_t aColor) {
    MOZ_ASSERT(AndroidBridge::IsJavaUiThread());
    mDefaultClearColor = Some((uint32_t)aColor);
    if (mUiCompositorControllerChild) {
      mUiCompositorControllerChild->SetDefaultClearColor((uint32_t)aColor);
    }
  }

  void RequestScreenPixels(jni::Object::Param aResult,
                           jni::Object::Param aTarget, int32_t aXOffset,
                           int32_t aYOffset, int32_t aSrcWidth,
                           int32_t aSrcHeight, int32_t aOutWidth,
                           int32_t aOutHeight) {
    MOZ_ASSERT(AndroidBridge::IsJavaUiThread());
    auto result = java::GeckoResult::LocalRef(aResult);

    if (!mUiCompositorControllerChild) {
      if (result) {
        if (auto window = mWindow.Access()) {
          result->CompleteExceptionally(
              java::sdk::IllegalStateException::New(
                  "Compositor session lost prior to screen pixels request")
                  .Cast<jni::Throwable>());
        }
      }
      return;
    }

    int size = 0;
    if (auto window = mWindow.Access()) {
      mCapturePixelsResults.push(CaptureRequest(
          java::GeckoResult::GlobalRef(result),
          java::sdk::Bitmap::GlobalRef(java::sdk::Bitmap::LocalRef(aTarget)),
          ScreenRect(aXOffset, aYOffset, aSrcWidth, aSrcHeight),
          IntSize(aOutWidth, aOutHeight)));
      size = mCapturePixelsResults.size();
    }

    if (size == 1) {
      mUiCompositorControllerChild->RequestScreenPixels();
    }
  }

  void RecvScreenPixels(Shmem&& aMem, const ScreenIntSize& aSize,
                        bool aNeedsYFlip) {
    MOZ_ASSERT(AndroidBridge::IsJavaUiThread());
    CaptureRequest request;
    java::GeckoResult::LocalRef result = nullptr;
    java::sdk::Bitmap::LocalRef bitmap = nullptr;
    if (auto window = mWindow.Access()) {
      // The result might have been already rejected if the compositor was
      // detached from the session
      if (!mCapturePixelsResults.empty()) {
        request = mCapturePixelsResults.front();
        result = java::GeckoResult::LocalRef(request.mResult);
        bitmap = java::sdk::Bitmap::LocalRef(request.mBitmap);
        mCapturePixelsResults.pop();
      }
    }

    if (result) {
      if (bitmap) {
        RefPtr<DataSourceSurface> surf;
        if (aNeedsYFlip) {
          surf = FlipScreenPixels(aMem, aSize, request.mSource,
                                  request.mOutputSize);
        } else {
          surf = gfx::Factory::CreateWrappingDataSourceSurface(
              aMem.get<uint8_t>(),
              StrideForFormatAndWidth(SurfaceFormat::B8G8R8A8, aSize.width),
              IntSize(aSize.width, aSize.height), SurfaceFormat::B8G8R8A8);
        }
        if (surf) {
          DataSourceSurface::ScopedMap smap(surf, DataSourceSurface::READ);
          auto pixels = mozilla::jni::ByteBuffer::New(
              reinterpret_cast<int8_t*>(smap.GetData()),
              smap.GetStride() * request.mOutputSize.height);
          bitmap->CopyPixelsFromBuffer(pixels);
          result->Complete(bitmap);
        } else {
          result->CompleteExceptionally(
              java::sdk::IllegalStateException::New(
                  "Failed to create flipped snapshot surface (probably out "
                  "of memory)")
                  .Cast<jni::Throwable>());
        }
      } else {
        result->CompleteExceptionally(java::sdk::IllegalArgumentException::New(
                                          "No target bitmap argument provided")
                                          .Cast<jni::Throwable>());
      }
    }

    // Pixels have been copied, so Dealloc Shmem
    if (mUiCompositorControllerChild) {
      mUiCompositorControllerChild->DeallocPixelBuffer(aMem);

      if (auto window = mWindow.Access()) {
        if (!mCapturePixelsResults.empty()) {
          mUiCompositorControllerChild->RequestScreenPixels();
        }
      }
    }
  }

  void EnableLayerUpdateNotifications(bool aEnable) {
    MOZ_ASSERT(AndroidBridge::IsJavaUiThread());
    if (mUiCompositorControllerChild) {
      mUiCompositorControllerChild->EnableLayerUpdateNotifications(aEnable);
    }
  }

  void OnSafeAreaInsetsChanged(int32_t aTop, int32_t aRight, int32_t aBottom,
                               int32_t aLeft) {
    MOZ_ASSERT(NS_IsMainThread());
    auto win(mWindow.Access());
    if (!win) {
      return;  // Already shut down.
    }

    nsWindow* gkWindow = win->GetNsWindow();
    if (!gkWindow) {
      return;
    }

    LayoutDeviceIntMargin safeAreaInsets(aTop, aRight, aBottom, aLeft);
    gkWindow->UpdateSafeAreaInsets(safeAreaInsets);
  }
};

GeckoViewSupport::~GeckoViewSupport() {
  if (mWindow) {
    mWindow->DetachNatives();
  }
}

/* static */
void GeckoViewSupport::Open(
    const jni::Class::LocalRef& aCls, GeckoSession::Window::Param aWindow,
    jni::Object::Param aQueue, jni::Object::Param aCompositor,
    jni::Object::Param aDispatcher, jni::Object::Param aSessionAccessibility,
    jni::Object::Param aInitData, jni::String::Param aId,
    jni::String::Param aChromeURI, bool aPrivateMode) {
  MOZ_ASSERT(NS_IsMainThread());

  AUTO_PROFILER_LABEL("mozilla::widget::GeckoViewSupport::Open", OTHER);

  // We'll need gfxPlatform to be initialized to create a compositor later.
  // Might as well do that now so that the GPU process launch can get a head
  // start.
  gfxPlatform::GetPlatform();

  nsCOMPtr<nsIWindowWatcher> ww = do_GetService(NS_WINDOWWATCHER_CONTRACTID);
  MOZ_RELEASE_ASSERT(ww);

  nsAutoCString url;
  if (aChromeURI) {
    url = aChromeURI->ToCString();
  } else {
    nsresult rv = Preferences::GetCString("toolkit.defaultChromeURI", url);
    if (NS_FAILED(rv)) {
      url = "chrome://geckoview/content/geckoview.xhtml"_ns;
    }
  }

  // Prepare an nsIGeckoViewView to pass as argument to the window.
  RefPtr<AndroidView> androidView = new AndroidView();
  androidView->mEventDispatcher->Attach(
      java::EventDispatcher::Ref::From(aDispatcher), nullptr);
  androidView->mInitData = java::GeckoBundle::Ref::From(aInitData);

  nsAutoCString chromeFlags("chrome,dialog=0,remote,resizable,scrollbars");
  if (aPrivateMode) {
    chromeFlags += ",private";
  }
  nsCOMPtr<mozIDOMWindowProxy> domWindow;
  ww->OpenWindow(nullptr, url, nsDependentCString(aId->ToCString().get()),
                 chromeFlags, androidView, getter_AddRefs(domWindow));
  MOZ_RELEASE_ASSERT(domWindow);

  nsCOMPtr<nsPIDOMWindowOuter> pdomWindow = nsPIDOMWindowOuter::From(domWindow);
  const RefPtr<nsWindow> window = nsWindow::From(pdomWindow);
  MOZ_ASSERT(window);

  // Attach a new GeckoView support object to the new window.
  GeckoSession::Window::LocalRef sessionWindow(aCls.Env(), aWindow);
  auto weakGeckoViewSupport =
      jni::NativeWeakPtrHolder<GeckoViewSupport>::Attach(
          sessionWindow, window, sessionWindow, pdomWindow);

  window->mGeckoViewSupport = weakGeckoViewSupport;
  window->mAndroidView = androidView;

  // Attach other session support objects.
  {  // Scope for gvsAccess
    auto gvsAccess = weakGeckoViewSupport.Access();
    MOZ_ASSERT(gvsAccess);

    gvsAccess->Transfer(sessionWindow, aQueue, aCompositor, aDispatcher,
                        aSessionAccessibility, aInitData);
  }

  if (window->mWidgetListener) {
    nsCOMPtr<nsIAppWindow> appWindow(window->mWidgetListener->GetAppWindow());
    if (appWindow) {
      // Our window is not intrinsically sized, so tell AppWindow to
      // not set a size for us.
      appWindow->SetIntrinsicallySized(false);
    }
  }
}

void GeckoViewSupport::Close() {
  if (mWindow) {
    if (mWindow->mAndroidView) {
      mWindow->mAndroidView->mEventDispatcher->Detach();
    }
    mWindow = nullptr;
  }

  if (!mDOMWindow) {
    return;
  }

  mDOMWindow->ForceClose();
  mDOMWindow = nullptr;
  mGeckoViewWindow = nullptr;
}

void GeckoViewSupport::Transfer(const GeckoSession::Window::LocalRef& inst,
                                jni::Object::Param aQueue,
                                jni::Object::Param aCompositor,
                                jni::Object::Param aDispatcher,
                                jni::Object::Param aSessionAccessibility,
                                jni::Object::Param aInitData) {
  mWindow->mNPZCSupport.Detach();

  auto compositor = GeckoSession::Compositor::LocalRef(
      inst.Env(), GeckoSession::Compositor::Ref::From(aCompositor));

  bool attachLvs;
  {  // Scope for lvsAccess
    auto lvsAccess{mWindow->mLayerViewSupport.Access()};
    // If we do not yet have mLayerViewSupport, or if the compositor has
    // changed, then we must attach a new one.
    attachLvs = !lvsAccess || lvsAccess->GetJavaCompositor() != compositor;
  }

  if (attachLvs) {
    mWindow->mLayerViewSupport =
        jni::NativeWeakPtrHolder<LayerViewSupport>::Attach(
            compositor, mWindow->mGeckoViewSupport, compositor);

    if (RefPtr<UiCompositorControllerChild> uiCompositorController =
            mWindow->GetUiCompositorControllerChild()) {
      DispatchToUiThread(
          "LayerViewSupport::NotifyCompositorCreated",
          [lvs = mWindow->mLayerViewSupport, uiCompositorController] {
            if (auto lvsAccess{lvs.Access()}) {
              lvsAccess->NotifyCompositorCreated(uiCompositorController);
            }
          });
    }
  }

  MOZ_ASSERT(mWindow->mAndroidView);
  mWindow->mAndroidView->mEventDispatcher->Attach(
      java::EventDispatcher::Ref::From(aDispatcher), mDOMWindow);

  RefPtr<jni::DetachPromise> promise = mWindow->mSessionAccessibility.Detach();
  if (aSessionAccessibility) {
    // SessionAccessibility's JNI object isn't released immediately, it uses
    // recycled object, we have to wait for released object completely.
    auto sa = java::SessionAccessibility::NativeProvider::LocalRef(
        aSessionAccessibility);
    promise->Then(
        GetMainThreadSerialEventTarget(),
        "GeckoViewSupprt::Transfer::SessionAccessibility",
        [inst = GeckoSession::Window::GlobalRef(inst),
         sa = java::SessionAccessibility::NativeProvider::GlobalRef(sa),
         window = mWindow, gvs = mWindow->mGeckoViewSupport](
            const mozilla::jni::DetachPromise::ResolveOrRejectValue& aValue) {
          MOZ_ASSERT(aValue.IsResolve());
          if (window->Destroyed()) {
            return;
          }

          MOZ_ASSERT(!window->mSessionAccessibility.IsAttached());
          if (auto gvsAccess{gvs.Access()}) {
            gvsAccess->AttachAccessibility(inst, sa);
          }
        });
  }

  if (mIsReady) {
    // We're in a transfer; update init-data and notify JS code.
    mWindow->mAndroidView->mInitData = java::GeckoBundle::Ref::From(aInitData);
    OnReady(aQueue);
    mWindow->mAndroidView->mEventDispatcher->Dispatch(
        u"GeckoView:UpdateInitData");
  }

  DispatchToUiThread("GeckoViewSupport::Transfer",
                     [compositor = GeckoSession::Compositor::GlobalRef(
                          compositor)] { compositor->OnCompositorAttached(); });
}

void GeckoViewSupport::AttachEditable(
    const GeckoSession::Window::LocalRef& inst,
    jni::Object::Param aEditableParent) {
  if (auto win{mWindow->mEditableSupport.Access()}) {
    win->TransferParent(aEditableParent);
  } else {
    auto editableChild = java::GeckoEditableChild::New(aEditableParent,
                                                       /* default */ true);
    mWindow->mEditableSupport =
        jni::NativeWeakPtrHolder<GeckoEditableSupport>::Attach(
            editableChild, mWindow->mGeckoViewSupport, editableChild);
  }

  mWindow->mEditableParent = aEditableParent;
}

void GeckoViewSupport::AttachAccessibility(
    const GeckoSession::Window::LocalRef& inst,
    jni::Object::Param aSessionAccessibility) {
  java::SessionAccessibility::NativeProvider::LocalRef sessionAccessibility(
      inst.Env());
  sessionAccessibility = java::SessionAccessibility::NativeProvider::Ref::From(
      aSessionAccessibility);

  mWindow->mSessionAccessibility =
      jni::NativeWeakPtrHolder<a11y::SessionAccessibility>::Attach(
          sessionAccessibility, mWindow->mGeckoViewSupport,
          sessionAccessibility);

  DispatchToUiThread(
      "GeckoViewSupport::AttachAccessibility",
      [sa = java::SessionAccessibility::NativeProvider::GlobalRef(
           sessionAccessibility)] { sa->SetAttached(true); });
}

auto GeckoViewSupport::OnLoadRequest(mozilla::jni::String::Param aUri,
                                     int32_t aWindowType, int32_t aFlags,
                                     mozilla::jni::String::Param aTriggeringUri,
                                     bool aHasUserGesture,
                                     bool aIsTopLevel) const
    -> java::GeckoResult::LocalRef {
  GeckoSession::Window::LocalRef window(mGeckoViewWindow);
  if (!window) {
    return nullptr;
  }
  return window->OnLoadRequest(aUri, aWindowType, aFlags, aTriggeringUri,
                               aHasUserGesture, aIsTopLevel);
}

void GeckoViewSupport::OnShowDynamicToolbar() const {
  GeckoSession::Window::LocalRef window(mGeckoViewWindow);
  if (!window) {
    return;
  }

  window->OnShowDynamicToolbar();
}

void GeckoViewSupport::OnHideDynamicToolbar() const {
  GeckoSession::Window::LocalRef window(mGeckoViewWindow);
  if (!window) {
    return;
  }

  window->OnHideDynamicToolbar();
}

void GeckoViewSupport::OnReady(jni::Object::Param aQueue) {
  GeckoSession::Window::LocalRef window(mGeckoViewWindow);
  if (!window) {
    return;
  }
  window->OnReady(aQueue);
  mIsReady = true;
}

void GeckoViewSupport::PassExternalResponse(
    java::WebResponse::Param aResponse) {
  GeckoSession::Window::LocalRef window(mGeckoViewWindow);
  if (!window) {
    return;
  }

  auto response = java::WebResponse::GlobalRef(aResponse);

  DispatchToUiThread("GeckoViewSupport::PassExternalResponse",
                     [window = java::GeckoSession::Window::GlobalRef(window),
                      response] { window->PassExternalWebResponse(response); });
}

RefPtr<CanonicalBrowsingContext>
GeckoViewSupport::GetContentCanonicalBrowsingContext() {
  nsCOMPtr<nsIDocShellTreeOwner> treeOwner = mDOMWindow->GetTreeOwner();
  if (!treeOwner) {
    return nullptr;
  }
  RefPtr<BrowsingContext> bc;
  nsresult rv = treeOwner->GetPrimaryContentBrowsingContext(getter_AddRefs(bc));
  if (NS_WARN_IF(NS_FAILED(rv)) || !bc) {
    return nullptr;
  }
  return bc->Canonical();
}

void GeckoViewSupport::CreatePdf(
    jni::LocalRef<mozilla::java::GeckoResult> aGeckoResult,
    RefPtr<dom::CanonicalBrowsingContext> aCbc) {
  MOZ_ASSERT(NS_IsMainThread());
  const auto pdfErrorMsg = "Could not save this page as PDF.";
  auto stream = java::GeckoInputStream::New(nullptr);
  RefPtr<GeckoViewOutputStream> streamListener =
      new GeckoViewOutputStream(stream);

  nsCOMPtr<nsIPrintSettingsService> printSettingsService =
      do_GetService("@mozilla.org/gfx/printsettings-service;1");
  if (!printSettingsService) {
    aGeckoResult->CompleteExceptionally(
        GeckoPrintException::New(
            GeckoPrintException::ERROR_PRINT_SETTINGS_SERVICE_NOT_AVAILABLE)
            .Cast<jni::Throwable>());
    GVS_LOG("Could not create print settings service.");
    return;
  }

  nsCOMPtr<nsIPrintSettings> printSettings;
  nsresult rv = printSettingsService->CreateNewPrintSettings(
      getter_AddRefs(printSettings));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aGeckoResult->CompleteExceptionally(
        GeckoPrintException::New(
            GeckoPrintException::ERROR_UNABLE_TO_CREATE_PRINT_SETTINGS)
            .Cast<jni::Throwable>());
    GVS_LOG("Could not create print settings.");
    return;
  }

  printSettings->SetPrinterName(u"Mozilla Save to PDF"_ns);
  printSettings->SetOutputDestination(
      nsIPrintSettings::kOutputDestinationStream);
  printSettings->SetOutputFormat(nsIPrintSettings::kOutputFormatPDF);
  printSettings->SetOutputStream(streamListener);
  printSettings->SetPrintSilent(true);

  RefPtr<CanonicalBrowsingContext::PrintPromise> print =
      aCbc->Print(printSettings);

  aGeckoResult->Complete(stream);
  print->Then(
      mozilla::GetCurrentSerialEventTarget(), __func__,
      [result = java::GeckoResult::GlobalRef(aGeckoResult), stream,
       pdfErrorMsg](
          const CanonicalBrowsingContext::PrintPromise::ResolveOrRejectValue&
              aValue) {
        if (aValue.IsReject()) {
          GVS_LOG("Could not print. %s", pdfErrorMsg);
          stream->WriteError();
        }
      });
}

void GeckoViewSupport::PrintToPdf(
    const java::GeckoSession::Window::LocalRef& inst,
    jni::Object::Param aResult) {
  auto geckoResult = java::GeckoResult::Ref::From(aResult);
  RefPtr<CanonicalBrowsingContext> cbc = GetContentCanonicalBrowsingContext();
  if (!cbc) {
    geckoResult->CompleteExceptionally(
        GeckoPrintException::New(
            GeckoPrintException::
                ERROR_UNABLE_TO_RETRIEVE_CANONICAL_BROWSING_CONTEXT)
            .Cast<jni::Throwable>());
    GVS_LOG("Could not retrieve content canonical browsing context.");
    return;
  }
  CreatePdf(geckoResult, cbc);
}

void GeckoViewSupport::PrintToPdf(
    const java::GeckoSession::Window::LocalRef& inst,
    jni::Object::Param aResult, int64_t aBcId) {
  auto geckoResult = java::GeckoResult::Ref::From(aResult);

  RefPtr<CanonicalBrowsingContext> cbc = CanonicalBrowsingContext::Get(aBcId);
  if (!cbc) {
    geckoResult->CompleteExceptionally(
        GeckoPrintException::New(
            GeckoPrintException::
                ERROR_UNABLE_TO_RETRIEVE_CANONICAL_BROWSING_CONTEXT)
            .Cast<jni::Throwable>());
    GVS_LOG("Could not retrieve content canonical browsing context by ID.");
    return;
  }
  CreatePdf(geckoResult, cbc);
}
}  // namespace widget
}  // namespace mozilla

void nsWindow::InitNatives() {
  jni::InitConversionStatics();
  mozilla::widget::GeckoViewSupport::Base::Init();
  mozilla::widget::LayerViewSupport::Init();
  mozilla::widget::NPZCSupport::Init();

  mozilla::widget::GeckoEditableSupport::Init();
  a11y::SessionAccessibility::Init();
}

void nsWindow::DetachNatives() {
  MOZ_ASSERT(NS_IsMainThread());
  mEditableSupport.Detach();
  mNPZCSupport.Detach();
  mLayerViewSupport.Detach();
  mSessionAccessibility.Detach();
}

/* static */
already_AddRefed<nsWindow> nsWindow::From(nsPIDOMWindowOuter* aDOMWindow) {
  nsCOMPtr<nsIWidget> widget = WidgetUtils::DOMWindowToWidget(aDOMWindow);
  return From(widget);
}

/* static */
already_AddRefed<nsWindow> nsWindow::From(nsIWidget* aWidget) {
  // `widget` may be one of several different types in the parent
  // process, including the Android nsWindow, PuppetWidget, etc. To
  // ensure that the cast to the Android nsWindow is valid, we check that the
  // widget is a top-level window and that its NS_NATIVE_WIDGET value is
  // non-null, which is not the case for non-native widgets like
  // PuppetWidget.
  if (aWidget && aWidget->GetWindowType() == WindowType::TopLevel &&
      aWidget->GetNativeData(NS_NATIVE_WIDGET) == aWidget) {
    RefPtr<nsWindow> window = static_cast<nsWindow*>(aWidget);
    return window.forget();
  }
  return nullptr;
}

nsWindow* nsWindow::TopWindow() {
  if (!gTopLevelWindows.IsEmpty()) return gTopLevelWindows[0];
  return nullptr;
}

void nsWindow::LogWindow(nsWindow* win, int index, int indent) {
#if defined(DEBUG) || defined(FORCE_ALOG)
  char spaces[] = "                    ";
  spaces[indent < 20 ? indent : 20] = 0;
  ALOG("%s [% 2d] 0x%p [parent 0x%p] [% 3d,% 3dx% 3d,% 3d] vis %d type %d",
       spaces, index, win, win->mParent, win->mBounds.x, win->mBounds.y,
       win->mBounds.width, win->mBounds.height, win->mIsVisible,
       int(win->mWindowType));
  int i = 0;
  for (nsIWidget* kid = win->mFirstChild; kid; kid = kid->GetNextSibling()) {
    LogWindow(static_cast<nsWindow*>(kid), i++, indent + 1);
  }
#endif
}

void nsWindow::DumpWindows() { DumpWindows(gTopLevelWindows); }

void nsWindow::DumpWindows(const nsTArray<nsWindow*>& wins, int indent) {
  for (uint32_t i = 0; i < wins.Length(); ++i) {
    nsWindow* w = wins[i];
    LogWindow(w, i, indent);
  }
}

nsWindow::nsWindow() : mWidgetId(++sWidgetId) {}

nsWindow::~nsWindow() {
  gTopLevelWindows.RemoveElement(this);
  ALOG("nsWindow %p destructor", (void*)this);
  // The mCompositorSession should have been cleaned up in nsWindow::Destroy()
  // DestroyLayerManager() will call DestroyCompositor() which will crash if
  // called from nsBaseWidget destructor. See Bug 1392705
  MOZ_ASSERT(!mCompositorSession);
}

bool nsWindow::IsTopLevel() {
  return mWindowType == WindowType::TopLevel ||
         mWindowType == WindowType::Dialog;
}

nsresult nsWindow::Create(nsIWidget* aParent, const LayoutDeviceIntRect& aRect,
                          InitData* aInitData) {
  ALOG("nsWindow[%p]::Create %p [%d %d %d %d]", (void*)this, (void*)aParent,
       aRect.x, aRect.y, aRect.width, aRect.height);

  // A default size of 1x1 confuses MobileViewportManager, so
  // use 0x0 instead. This is also a little more fitting since
  // we don't yet have a surface yet (and therefore a valid size)
  // and 0x0 is usually recognized as invalid.
  LayoutDeviceIntRect rect = aRect;
  if (aRect.width == 1 && aRect.height == 1) {
    rect.width = 0;
    rect.height = 0;
  }

  mBounds = rect;
  SetSizeConstraints(SizeConstraints());

  MOZ_DIAGNOSTIC_ASSERT(!aInitData ||
                        aInitData->mWindowType != WindowType::Invisible);

  BaseCreate(aParent, aInitData);
  MOZ_ASSERT_IF(!IsTopLevel(), aParent);

  if (IsTopLevel()) {
    gTopLevelWindows.AppendElement(this);
  }

#ifdef DEBUG_ANDROID_WIDGET
  DumpWindows();
#endif

  return NS_OK;
}

void nsWindow::Destroy() {
  MutexAutoLock lock(mDestroyMutex);

  nsBaseWidget::mOnDestroyCalled = true;

  // Disassociate our native object from GeckoView.
  mGeckoViewSupport.Detach();

  // Stuff below may release the last ref to this
  nsCOMPtr<nsIWidget> kungFuDeathGrip(this);

  // Ensure the compositor has been shutdown before this nsWindow is potentially
  // deleted
  nsBaseWidget::DestroyCompositor();

  nsBaseWidget::Destroy();

  if (IsTopLevel()) {
    gTopLevelWindows.RemoveElement(this);
  }

  nsBaseWidget::OnDestroy();

#ifdef DEBUG_ANDROID_WIDGET
  DumpWindows();
#endif
}

mozilla::widget::EventDispatcher* nsWindow::GetEventDispatcher() const {
  if (mAndroidView) {
    return mAndroidView->mEventDispatcher;
  }
  return nullptr;
}

void nsWindow::RedrawAll() {
  if (mAttachedWidgetListener) {
    mAttachedWidgetListener->RequestRepaint();
  } else if (mWidgetListener) {
    mWidgetListener->RequestRepaint();
  }
}

RefPtr<UiCompositorControllerChild> nsWindow::GetUiCompositorControllerChild() {
  return mCompositorSession
             ? mCompositorSession->GetUiCompositorControllerChild()
             : nullptr;
}

mozilla::layers::LayersId nsWindow::GetRootLayerId() const {
  return mCompositorSession ? mCompositorSession->RootLayerTreeId()
                            : mozilla::layers::LayersId{0};
}

void nsWindow::OnGeckoViewReady() {
  auto acc(mGeckoViewSupport.Access());
  if (!acc) {
    return;
  }

  acc->OnReady();
}

void nsWindow::DidClearParent(nsIWidget*) {
  // if we are now in the toplevel window's hierarchy, schedule a redraw
  if (FindTopLevel() == nsWindow::TopWindow()) {
    RedrawAll();
  }
}

RefPtr<MozPromise<bool, bool, false>> nsWindow::OnLoadRequest(
    nsIURI* aUri, int32_t aWindowType, int32_t aFlags,
    nsIPrincipal* aTriggeringPrincipal, bool aHasUserGesture,
    bool aIsTopLevel) {
  auto geckoViewSupport(mGeckoViewSupport.Access());
  if (!geckoViewSupport) {
    return MozPromise<bool, bool, false>::CreateAndResolve(false, __func__);
  }

  nsAutoCString spec, triggeringSpec;
  if (aUri) {
    aUri->GetDisplaySpec(spec);
    if (aIsTopLevel && aUri->SchemeIs("data") &&
        spec.Length() > MAX_TOPLEVEL_DATA_URI_LEN) {
      return MozPromise<bool, bool, false>::CreateAndResolve(false, __func__);
    }
  }

  bool isNullPrincipal = false;
  if (aTriggeringPrincipal) {
    aTriggeringPrincipal->GetIsNullPrincipal(&isNullPrincipal);

    if (!isNullPrincipal) {
      nsCOMPtr<nsIURI> triggeringUri;
      BasePrincipal::Cast(aTriggeringPrincipal)
          ->GetURI(getter_AddRefs(triggeringUri));
      if (triggeringUri) {
        triggeringUri->GetDisplaySpec(triggeringSpec);
      }
    }
  }

  auto geckoResult = geckoViewSupport->OnLoadRequest(
      spec.get(), aWindowType, aFlags,
      isNullPrincipal ? nullptr : triggeringSpec.get(), aHasUserGesture,
      aIsTopLevel);
  return geckoResult
             ? MozPromise<bool, bool, false>::FromGeckoResult(geckoResult)
             : nullptr;
}

float nsWindow::GetDPI() {
  float dpi = 160.0f;

  nsCOMPtr<nsIScreen> screen = GetWidgetScreen();
  if (screen) {
    screen->GetDpi(&dpi);
  }

  return dpi;
}

double nsWindow::GetDefaultScaleInternal() {
  double scale = 1.0f;

  nsCOMPtr<nsIScreen> screen = GetWidgetScreen();
  if (screen) {
    screen->GetContentsScaleFactor(&scale);
  }

  return scale;
}

void nsWindow::Show(bool aState) {
  ALOG("nsWindow[%p]::Show %d", (void*)this, aState);

  if (mWindowType == WindowType::Invisible) {
    ALOG("trying to show invisible window! ignoring..");
    return;
  }

  if (aState == mIsVisible) return;

  mIsVisible = aState;

  if (IsTopLevel()) {
    // XXX should we bring this to the front when it's shown,
    // if it's a toplevel widget?

    // XXX we should synthesize a eMouseExitFromWidget (for old top
    // window)/eMouseEnterIntoWidget (for new top window) since we need
    // to pretend that the top window always has focus.  Not sure
    // if Show() is the right place to do this, though.

    if (aState) {
      // It just became visible, so bring it to the front.
      BringToFront();

    } else if (nsWindow::TopWindow() == this) {
      // find the next visible window to show
      unsigned int i;
      for (i = 1; i < gTopLevelWindows.Length(); i++) {
        nsWindow* win = gTopLevelWindows[i];
        if (!win->mIsVisible) {
          continue;
        }

        win->BringToFront();
        break;
      }
    }
  } else if (FindTopLevel() == nsWindow::TopWindow()) {
    RedrawAll();
  }

#ifdef DEBUG_ANDROID_WIDGET
  DumpWindows();
#endif
}

bool nsWindow::IsVisible() const { return mIsVisible; }

void nsWindow::ConstrainPosition(DesktopIntPoint& aPoint) {
  ALOG("nsWindow[%p]::ConstrainPosition [%d %d]", (void*)this, aPoint.x.value,
       aPoint.y.value);

  // Constrain toplevel windows; children we don't care about
  if (IsTopLevel()) {
    aPoint = DesktopIntPoint();
  }
}

void nsWindow::Move(double aX, double aY) {
  if (IsTopLevel()) return;

  Resize(aX, aY, mBounds.width, mBounds.height, true);
}

void nsWindow::Resize(double aWidth, double aHeight, bool aRepaint) {
  Resize(mBounds.x, mBounds.y, aWidth, aHeight, aRepaint);
}

void nsWindow::Resize(double aX, double aY, double aWidth, double aHeight,
                      bool aRepaint) {
  ALOG("nsWindow[%p]::Resize [%f %f %f %f] (repaint %d)", (void*)this, aX, aY,
       aWidth, aHeight, aRepaint);

  LayoutDeviceIntRect oldBounds = mBounds;

  mBounds.x = NSToIntRound(aX);
  mBounds.y = NSToIntRound(aY);
  mBounds.width = NSToIntRound(aWidth);
  mBounds.height = NSToIntRound(aHeight);

  ConstrainSize(&mBounds.width, &mBounds.height);

  bool needPositionDispatch = mBounds.TopLeft() != oldBounds.TopLeft();
  bool needSizeDispatch = mBounds.Size() != oldBounds.Size();

  if (needSizeDispatch) {
    OnSizeChanged(mBounds.Size().ToUnknownSize());
  }

  if (needPositionDispatch) {
    NotifyWindowMoved(mBounds.x, mBounds.y);
  }

  // Should we skip honoring aRepaint here?
  if (aRepaint && FindTopLevel() == nsWindow::TopWindow()) RedrawAll();
}

void nsWindow::SetSizeMode(nsSizeMode aMode) {
  if (aMode == mSizeMode) {
    return;
  }

  mSizeMode = aMode;

  switch (aMode) {
    case nsSizeMode_Minimized:
      java::GeckoAppShell::MoveTaskToBack();
      break;
    case nsSizeMode_Fullscreen:
      MakeFullScreen(true);
      break;
    default:
      break;
  }
}

void nsWindow::Enable(bool aState) {
  ALOG("nsWindow[%p]::Enable %d ignored", (void*)this, aState);
}

bool nsWindow::IsEnabled() const { return true; }

void nsWindow::Invalidate(const LayoutDeviceIntRect& aRect) {}

nsWindow* nsWindow::FindTopLevel() {
  nsWindow* toplevel = this;
  while (toplevel) {
    if (toplevel->IsTopLevel()) {
      return toplevel;
    }
    toplevel = static_cast<nsWindow*>(toplevel->mParent);
  }

  ALOG(
      "nsWindow::FindTopLevel(): couldn't find a toplevel or dialog window in "
      "this [%p] widget's hierarchy!",
      (void*)this);
  return this;
}

void nsWindow::SetFocus(Raise, mozilla::dom::CallerType aCallerType) {
  FindTopLevel()->BringToFront();
}

void nsWindow::BringToFront() {
  MOZ_ASSERT(XRE_IsParentProcess());
  // If the window to be raised is the same as the currently raised one,
  // do nothing. We need to check the focus manager as well, as the first
  // window that is created will be first in the window list but won't yet
  // be focused.
  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (fm && fm->GetActiveWindow() && FindTopLevel() == nsWindow::TopWindow()) {
    return;
  }

  if (!IsTopLevel()) {
    FindTopLevel()->BringToFront();
    return;
  }

  RefPtr<nsWindow> kungFuDeathGrip(this);

  nsWindow* oldTop = nullptr;
  if (!gTopLevelWindows.IsEmpty()) {
    oldTop = gTopLevelWindows[0];
  }

  gTopLevelWindows.RemoveElement(this);
  gTopLevelWindows.InsertElementAt(0, this);

  if (oldTop) {
    nsIWidgetListener* listener = oldTop->GetWidgetListener();
    if (listener) {
      listener->WindowDeactivated();
    }
  }

  if (mWidgetListener) {
    mWidgetListener->WindowActivated();
  }

  RedrawAll();
}

LayoutDeviceIntRect nsWindow::GetScreenBounds() {
  return LayoutDeviceIntRect(WidgetToScreenOffset(), mBounds.Size());
}

LayoutDeviceIntPoint nsWindow::WidgetToScreenOffset() {
  LayoutDeviceIntPoint p(0, 0);

  for (nsWindow* w = this; !!w; w = static_cast<nsWindow*>(w->mParent)) {
    p += w->mBounds.TopLeft();
    if (w->IsTopLevel()) {
      break;
    }
  }
  return p;
}

nsresult nsWindow::DispatchEvent(WidgetGUIEvent* aEvent,
                                 nsEventStatus& aStatus) {
  aStatus = DispatchEvent(aEvent);
  return NS_OK;
}

nsEventStatus nsWindow::DispatchEvent(WidgetGUIEvent* aEvent) {
  if (mAttachedWidgetListener) {
    return mAttachedWidgetListener->HandleEvent(aEvent, mUseAttachedEvents);
  } else if (mWidgetListener) {
    return mWidgetListener->HandleEvent(aEvent, mUseAttachedEvents);
  }
  return nsEventStatus_eIgnore;
}

nsresult nsWindow::MakeFullScreen(bool aFullScreen) {
  if (!mAndroidView) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mIsFullScreen = aFullScreen;
  mAndroidView->mEventDispatcher->Dispatch(
      aFullScreen ? u"GeckoView:FullScreenEnter" : u"GeckoView:FullScreenExit");

  nsIWidgetListener* listener = GetWidgetListener();
  if (listener) {
    mSizeMode = mIsFullScreen ? nsSizeMode_Fullscreen : nsSizeMode_Normal;
    listener->SizeModeChanged(mSizeMode);
  }
  return NS_OK;
}

mozilla::WindowRenderer* nsWindow::GetWindowRenderer() {
  if (!mWindowRenderer) {
    CreateLayerManager();
  }

  return mWindowRenderer;
}

void nsWindow::CreateLayerManager() {
  if (mWindowRenderer) {
    return;
  }

  nsWindow* topLevelWindow = FindTopLevel();
  if (!topLevelWindow || topLevelWindow->mWindowType == WindowType::Invisible) {
    // don't create a layer manager for an invisible top-level window
    return;
  }

  // Ensure that gfxPlatform is initialized first.
  gfxPlatform::GetPlatform();

  if (ShouldUseOffMainThreadCompositing()) {
    LayoutDeviceIntRect rect = GetBounds();
    CreateCompositor(rect.Width(), rect.Height());
    if (mWindowRenderer) {
      if (mLayerViewSupport.IsAttached()) {
        DispatchToUiThread(
            "LayerViewSupport::NotifyCompositorCreated",
            [lvs = mLayerViewSupport,
             uiCompositorController = GetUiCompositorControllerChild()] {
              if (auto lvsAccess{lvs.Access()}) {
                lvsAccess->NotifyCompositorCreated(uiCompositorController);
              }
            });
      }

      return;
    }

    // If we get here, then off main thread compositing failed to initialize.
    sFailedToCreateGLContext = true;
  }

  if (!ComputeShouldAccelerate() || sFailedToCreateGLContext) {
    printf_stderr(" -- creating basic, not accelerated\n");
    mWindowRenderer = CreateFallbackRenderer();
  }
}

void nsWindow::NotifyCompositorSessionLost(
    mozilla::layers::CompositorSession* aSession) {
  nsBaseWidget::NotifyCompositorSessionLost(aSession);

  DispatchToUiThread("nsWindow::NotifyCompositorSessionLost",
                     [lvs = mLayerViewSupport] {
                       if (auto lvsAccess{lvs.Access()}) {
                         lvsAccess->NotifyCompositorSessionLost();
                       }
                     });

  RedrawAll();
}

void nsWindow::ShowDynamicToolbar() {
  auto acc(mGeckoViewSupport.Access());
  if (!acc) {
    return;
  }

  acc->OnShowDynamicToolbar();
}

void nsWindow::OnDragEvent(int32_t aAction, float aX, float aY,
                           jni::Object::Param aDropData,
                           const mozilla::layers::APZEventResult& aApzResult,
                           const MouseInput& aInput) {
  MOZ_ASSERT(NS_IsMainThread());

  LayoutDeviceIntPoint point =
      LayoutDeviceIntPoint(int32_t(floorf(aX)), int32_t(floorf(aY)));

  RefPtr<nsDragService> dragService = nsDragService::GetInstance();
  if (!dragService) {
    return;
  }

  RefPtr<nsDragSession> dragSession =
      static_cast<nsDragSession*>(dragService->GetCurrentSession(this));
  if (aAction == java::sdk::DragEvent::ACTION_DRAG_STARTED) {
    if (dragSession) {
      dragSession->SetDragEndPoint(point.x, point.y);
    }
    return;
  }

  if (aAction == java::sdk::DragEvent::ACTION_DRAG_ENDED) {
    if (dragSession) {
      dragSession->EndDragSession(false, 0);
    }
    return;
  }

  if (aAction == java::sdk::DragEvent::ACTION_DRAG_ENTERED) {
    nsIWidget* widget = this;
    dragSession =
        static_cast<nsDragSession*>(dragService->StartDragSession(widget));
    // For compatibility, we have to set temporary data.
    auto dropData =
        mozilla::java::GeckoDragAndDrop::DropData::Ref::From(aDropData);
    dragSession->SetDropData(dropData);
  }

  if (dragSession) {
    switch (aAction) {
      case java::sdk::DragEvent::ACTION_DRAG_LOCATION:
        dragSession->SetDragEndPoint(point.x, point.y);
        dragSession->FireDragEventAtSource(eDrag, 0);
        break;
      case java::sdk::DragEvent::ACTION_DROP: {
        bool canDrop = false;
        dragSession->GetCanDrop(&canDrop);
        if (!canDrop) {
          nsCOMPtr<nsINode> sourceNode;
          dragSession->GetSourceNode(getter_AddRefs(sourceNode));
          if (!sourceNode) {
            dragSession->EndDragSession(false, 0);
          }
          return;
        }
        auto dropData =
            mozilla::java::GeckoDragAndDrop::DropData::Ref::From(aDropData);
        dragSession->SetDropData(dropData);
        dragSession->SetDragEndPoint(point.x, point.y);
        break;
      }
      default:
        break;
    }

    dragSession->SetDragAction(nsIDragService::DRAGDROP_ACTION_MOVE);
  }

  WidgetDragEvent geckoEvent = aInput.ToWidgetEvent<WidgetDragEvent>(this);
  ProcessUntransformedAPZEvent(&geckoEvent, aApzResult);

  if (!dragSession) {
    return;
  }

  switch (aAction) {
    case java::sdk::DragEvent::ACTION_DRAG_EXITED: {
      nsCOMPtr<nsINode> sourceNode;
      dragSession->GetSourceNode(getter_AddRefs(sourceNode));
      if (!sourceNode) {
        // We're leaving a window while doing a drag that was
        // initiated in a different app. End the drag session,
        // since we're done with it for now (until the user
        // drags back into mozilla).
        dragSession->EndDragSession(false, 0);
      }
      break;
    }
    case java::sdk::DragEvent::ACTION_DROP:
      dragSession->EndDragSession(true, 0);
      break;
    default:
      break;
  }
}

void nsWindow::StartDragAndDrop(java::sdk::Bitmap::LocalRef aBitmap) {
  if (mozilla::jni::NativeWeakPtr<LayerViewSupport>::Accessor lvs{
          mLayerViewSupport.Access()}) {
    const auto& compositor = lvs->GetJavaCompositor();

    DispatchToUiThread(
        "nsWindow::StartDragAndDrop",
        [compositor = GeckoSession::Compositor::GlobalRef(compositor),
         bitmap = java::sdk::Bitmap::GlobalRef(aBitmap)] {
          compositor->StartDragAndDrop(bitmap);
        });
  }
}

void nsWindow::UpdateDragImage(java::sdk::Bitmap::LocalRef aBitmap) {
  if (mozilla::jni::NativeWeakPtr<LayerViewSupport>::Accessor lvs{
          mLayerViewSupport.Access()}) {
    const auto& compositor = lvs->GetJavaCompositor();

    DispatchToUiThread(
        "nsWindow::UpdateDragImage",
        [compositor = GeckoSession::Compositor::GlobalRef(compositor),
         bitmap = java::sdk::Bitmap::GlobalRef(aBitmap)] {
          compositor->UpdateDragImage(bitmap);
        });
  }
}

void nsWindow::OnSizeChanged(const gfx::IntSize& aSize) {
  ALOG("nsWindow: %p OnSizeChanged [%d %d]", (void*)this, aSize.width,
       aSize.height);

  if (mWidgetListener) {
    mWidgetListener->WindowResized(this, aSize.width, aSize.height);
  }

  if (mAttachedWidgetListener) {
    mAttachedWidgetListener->WindowResized(this, aSize.width, aSize.height);
  }

  if (mCompositorWidgetDelegate) {
    mCompositorWidgetDelegate->NotifyClientSizeChanged(
        LayoutDeviceIntSize::FromUnknownSize(aSize));
  }
}

void nsWindow::InitEvent(WidgetGUIEvent& event, LayoutDeviceIntPoint* aPoint) {
  if (aPoint) {
    event.mRefPoint = *aPoint;
  } else {
    event.mRefPoint = LayoutDeviceIntPoint(0, 0);
  }
}

void nsWindow::UpdateOverscrollVelocity(const float aX, const float aY) {
  if (::mozilla::jni::NativeWeakPtr<LayerViewSupport>::Accessor lvs{
          mLayerViewSupport.Access()}) {
    const auto& compositor = lvs->GetJavaCompositor();
    if (AndroidBridge::IsJavaUiThread()) {
      compositor->UpdateOverscrollVelocity(aX, aY);
      return;
    }

    DispatchToUiThread(
        "nsWindow::UpdateOverscrollVelocity",
        [compositor = GeckoSession::Compositor::GlobalRef(compositor), aX, aY] {
          compositor->UpdateOverscrollVelocity(aX, aY);
        });
  }
}

void nsWindow::UpdateOverscrollOffset(const float aX, const float aY) {
  if (::mozilla::jni::NativeWeakPtr<LayerViewSupport>::Accessor lvs{
          mLayerViewSupport.Access()}) {
    const auto& compositor = lvs->GetJavaCompositor();
    if (AndroidBridge::IsJavaUiThread()) {
      compositor->UpdateOverscrollOffset(aX, aY);
      return;
    }

    DispatchToUiThread(
        "nsWindow::UpdateOverscrollOffset",
        [compositor = GeckoSession::Compositor::GlobalRef(compositor), aX, aY] {
          compositor->UpdateOverscrollOffset(aX, aY);
        });
  }
}

void nsWindow::HideDynamicToolbar() {
  auto acc(mGeckoViewSupport.Access());
  if (!acc) {
    return;
  }

  acc->OnHideDynamicToolbar();
}

void* nsWindow::GetNativeData(uint32_t aDataType) {
  switch (aDataType) {
    // used by GLContextProviderEGL, nullptr is EGL_DEFAULT_DISPLAY
    case NS_NATIVE_WIDGET:
      return (void*)this;

    case NS_RAW_NATIVE_IME_CONTEXT: {
      void* pseudoIMEContext = GetPseudoIMEContext();
      if (pseudoIMEContext) {
        return pseudoIMEContext;
      }
      // We assume that there is only one context per process on Android
      return NS_ONLY_ONE_NATIVE_IME_CONTEXT;
    }

    case NS_JAVA_SURFACE:
      if (::mozilla::jni::NativeWeakPtr<LayerViewSupport>::Accessor lvs{
              mLayerViewSupport.Access()}) {
        return lvs->GetSurface().Get();
      }
      return nullptr;
  }

  return nullptr;
}

void nsWindow::DispatchHitTest(const WidgetTouchEvent& aEvent) {
  if (aEvent.mMessage == eTouchStart && aEvent.mTouches.Length() == 1) {
    // Since touch events don't get retargeted by PositionedEventTargeting.cpp
    // code, we dispatch a dummy mouse event that *does* get retargeted.
    // Front-end code can use this to activate the highlight element in case
    // this touchstart is the start of a tap.
    WidgetMouseEvent hittest(true, eMouseHitTest, this,
                             WidgetMouseEvent::eReal);
    hittest.mRefPoint = aEvent.mTouches[0]->mRefPoint;
    nsEventStatus status;
    DispatchEvent(&hittest, status);
  }
}

void nsWindow::PassExternalResponse(java::WebResponse::Param aResponse) {
  if (Destroyed()) {
    return;
  }

  auto acc(mGeckoViewSupport.Access());
  if (!acc) {
    return;
  }

  acc->PassExternalResponse(aResponse);
}

mozilla::Modifiers nsWindow::GetModifiers(int32_t metaState) {
  using mozilla::java::sdk::KeyEvent;
  return (metaState & KeyEvent::META_ALT_MASK ? MODIFIER_ALT : 0) |
         (metaState & KeyEvent::META_SHIFT_MASK ? MODIFIER_SHIFT : 0) |
         (metaState & KeyEvent::META_CTRL_MASK ? MODIFIER_CONTROL : 0) |
         (metaState & KeyEvent::META_META_MASK ? MODIFIER_META : 0) |
         (metaState & KeyEvent::META_FUNCTION_ON ? MODIFIER_FN : 0) |
         (metaState & KeyEvent::META_CAPS_LOCK_ON ? MODIFIER_CAPSLOCK : 0) |
         (metaState & KeyEvent::META_NUM_LOCK_ON ? MODIFIER_NUMLOCK : 0) |
         (metaState & KeyEvent::META_SCROLL_LOCK_ON ? MODIFIER_SCROLLLOCK : 0);
}

TimeStamp nsWindow::GetEventTimeStamp(int64_t aEventTime) {
  // Android's event time is SystemClock.uptimeMillis that is counted in ms
  // since OS was booted.
  // (https://developer.android.com/reference/android/os/SystemClock.html)
  // and this SystemClock.uptimeMillis uses SYSTEM_TIME_MONOTONIC.
  // Our posix implemententaion of TimeStamp::Now uses SYSTEM_TIME_MONOTONIC
  //  too. Due to same implementation, we can use this via FromSystemTime.
  int64_t tick =
      BaseTimeDurationPlatformUtils::TicksFromMilliseconds(aEventTime);
  return TimeStamp::FromSystemTime(tick);
}

void nsWindow::UserActivity() {
  if (!mIdleService) {
    mIdleService = do_GetService("@mozilla.org/widget/useridleservice;1");
  }

  if (mIdleService) {
    mIdleService->ResetIdleTimeOut(0);
  }

  if (FindTopLevel() != nsWindow::TopWindow()) {
    BringToFront();
  }
}

RefPtr<mozilla::a11y::SessionAccessibility>
nsWindow::GetSessionAccessibility() {
  auto acc(mSessionAccessibility.Access());
  if (!acc) {
    return nullptr;
  }

  return acc.AsRefPtr();
}

TextEventDispatcherListener* nsWindow::GetNativeTextEventDispatcherListener() {
  nsWindow* top = FindTopLevel();
  MOZ_ASSERT(top);

  auto acc(top->mEditableSupport.Access());
  if (!acc) {
    // Non-GeckoView windows don't support IME operations.
    return nullptr;
  }

  nsCOMPtr<TextEventDispatcherListener> ptr;
  if (NS_FAILED(acc->QueryInterface(NS_GET_IID(TextEventDispatcherListener),
                                    getter_AddRefs(ptr)))) {
    return nullptr;
  }

  return ptr.get();
}

void nsWindow::SetInputContext(const InputContext& aContext,
                               const InputContextAction& aAction) {
  nsWindow* top = FindTopLevel();
  MOZ_ASSERT(top);

  auto acc(top->mEditableSupport.Access());
  if (!acc) {
    // Non-GeckoView windows don't support IME operations.
    return;
  }

  // We are using an IME event later to notify Java, and the IME event
  // will be processed by the top window. Therefore, to ensure the
  // IME event uses the correct mInputContext, we need to let the top
  // window process SetInputContext
  acc->SetInputContext(aContext, aAction);
}

InputContext nsWindow::GetInputContext() {
  nsWindow* top = FindTopLevel();
  MOZ_ASSERT(top);

  auto acc(top->mEditableSupport.Access());
  if (!acc) {
    // Non-GeckoView windows don't support IME operations.
    return InputContext();
  }

  // We let the top window process SetInputContext,
  // so we should let it process GetInputContext as well.
  return acc->GetInputContext();
}

nsresult nsWindow::SynthesizeNativeTouchPoint(uint32_t aPointerId,
                                              TouchPointerState aPointerState,
                                              LayoutDeviceIntPoint aPoint,
                                              double aPointerPressure,
                                              uint32_t aPointerOrientation,
                                              nsIObserver* aObserver) {
  mozilla::widget::AutoObserverNotifier notifier(aObserver, "touchpoint");

  int eventType;
  switch (aPointerState) {
    case TOUCH_CONTACT:
      // This could be a ACTION_DOWN or ACTION_MOVE depending on the
      // existing state; it is mapped to the right thing in Java.
      eventType = java::sdk::MotionEvent::ACTION_POINTER_DOWN;
      break;
    case TOUCH_REMOVE:
      // This could be turned into a ACTION_UP in Java
      eventType = java::sdk::MotionEvent::ACTION_POINTER_UP;
      break;
    case TOUCH_CANCEL:
      eventType = java::sdk::MotionEvent::ACTION_CANCEL;
      break;
    case TOUCH_HOVER:  // not supported for now
    default:
      return NS_ERROR_UNEXPECTED;
  }

  MOZ_ASSERT(mNPZCSupport.IsAttached());
  auto npzcSup(mNPZCSupport.Access());
  MOZ_ASSERT(!!npzcSup);

  const auto& npzc = npzcSup->GetJavaNPZC();
  const auto& bounds = FindTopLevel()->mBounds;
  aPoint -= bounds.TopLeft();

  DispatchToUiThread(
      "nsWindow::SynthesizeNativeTouchPoint",
      [npzc = java::PanZoomController::NativeProvider::GlobalRef(npzc),
       aPointerId, eventType, aPoint, aPointerPressure, aPointerOrientation] {
        npzc->SynthesizeNativeTouchPoint(aPointerId, eventType, aPoint.x,
                                         aPoint.y, aPointerPressure,
                                         aPointerOrientation);
      });
  return NS_OK;
}

nsresult nsWindow::SynthesizeNativeMouseEvent(
    LayoutDeviceIntPoint aPoint, NativeMouseMessage aNativeMessage,
    MouseButton aButton, nsIWidget::Modifiers aModifierFlags,
    nsIObserver* aObserver) {
  mozilla::widget::AutoObserverNotifier notifier(aObserver, "mouseevent");

  MOZ_ASSERT(mNPZCSupport.IsAttached());
  auto npzcSup(mNPZCSupport.Access());
  MOZ_ASSERT(!!npzcSup);

  const auto& npzc = npzcSup->GetJavaNPZC();
  const auto& bounds = FindTopLevel()->mBounds;
  aPoint -= bounds.TopLeft();

  int32_t nativeMessage;
  switch (aNativeMessage) {
    case NativeMouseMessage::ButtonDown:
      nativeMessage = java::sdk::MotionEvent::ACTION_POINTER_DOWN;
      break;
    case NativeMouseMessage::ButtonUp:
      nativeMessage = java::sdk::MotionEvent::ACTION_POINTER_UP;
      break;
    case NativeMouseMessage::Move:
      nativeMessage = java::sdk::MotionEvent::ACTION_HOVER_MOVE;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Non supported mouse event on Android");
      return NS_ERROR_INVALID_ARG;
  }
  int32_t button = 0;
  if (aNativeMessage != NativeMouseMessage::ButtonUp) {
    switch (aButton) {
      case MouseButton::ePrimary:
        button = java::sdk::MotionEvent::BUTTON_PRIMARY;
        break;
      case MouseButton::eMiddle:
        button = java::sdk::MotionEvent::BUTTON_TERTIARY;
        break;
      case MouseButton::eSecondary:
        button = java::sdk::MotionEvent::BUTTON_SECONDARY;
        break;
      case MouseButton::eX1:
        button = java::sdk::MotionEvent::BUTTON_BACK;
        break;
      case MouseButton::eX2:
        button = java::sdk::MotionEvent::BUTTON_FORWARD;
        break;
      default:
        if (aNativeMessage == NativeMouseMessage::ButtonDown) {
          MOZ_ASSERT_UNREACHABLE("Non supported mouse button type on Android");
          return NS_ERROR_INVALID_ARG;
        }
        break;
    }
  }

  // TODO (bug 1693237): Handle aModifierFlags.
  DispatchToUiThread(
      "nsWindow::SynthesizeNativeMouseEvent",
      [npzc = java::PanZoomController::NativeProvider::GlobalRef(npzc),
       nativeMessage, aPoint, button] {
        npzc->SynthesizeNativeMouseEvent(nativeMessage, aPoint.x, aPoint.y,
                                         button);
      });
  return NS_OK;
}

nsresult nsWindow::SynthesizeNativeMouseMove(LayoutDeviceIntPoint aPoint,
                                             nsIObserver* aObserver) {
  return SynthesizeNativeMouseEvent(
      aPoint, NativeMouseMessage::Move, MouseButton::eNotPressed,
      nsIWidget::Modifiers::NO_MODIFIERS, aObserver);
}

void nsWindow::SetCompositorWidgetDelegate(CompositorWidgetDelegate* delegate) {
  if (delegate) {
    mCompositorWidgetDelegate = delegate->AsPlatformSpecificDelegate();
    MOZ_ASSERT(mCompositorWidgetDelegate,
               "nsWindow::SetCompositorWidgetDelegate called with a "
               "non-PlatformCompositorWidgetDelegate");
  } else {
    mCompositorWidgetDelegate = nullptr;
  }
}

void nsWindow::GetCompositorWidgetInitData(
    mozilla::widget::CompositorWidgetInitData* aInitData) {
  *aInitData = mozilla::widget::AndroidCompositorWidgetInitData(
      mWidgetId, GetClientSize());
}

bool nsWindow::WidgetPaintsBackground() {
  return StaticPrefs::android_widget_paints_background();
}

bool nsWindow::NeedsPaint() {
  auto lvs(mLayerViewSupport.Access());
  if (!lvs || lvs->CompositorPaused() || !GetWindowRenderer()) {
    return false;
  }

  return nsIWidget::NeedsPaint();
}

void nsWindow::ConfigureAPZControllerThread() {
  nsCOMPtr<nsISerialEventTarget> thread = mozilla::GetAndroidUiThread();
  APZThreadUtils::SetControllerThread(thread);
}

already_AddRefed<GeckoContentController>
nsWindow::CreateRootContentController() {
  RefPtr<GeckoContentController> controller =
      new AndroidContentController(this, mAPZEventState, mAPZC);
  return controller.forget();
}

uint32_t nsWindow::GetMaxTouchPoints() const {
  return java::GeckoAppShell::GetMaxTouchPoints();
}

void nsWindow::UpdateZoomConstraints(
    const uint32_t& aPresShellId, const ScrollableLayerGuid::ViewID& aViewId,
    const mozilla::Maybe<ZoomConstraints>& aConstraints) {
  nsBaseWidget::UpdateZoomConstraints(aPresShellId, aViewId, aConstraints);
}

CompositorBridgeChild* nsWindow::GetCompositorBridgeChild() const {
  return mCompositorSession ? mCompositorSession->GetCompositorBridgeChild()
                            : nullptr;
}

void nsWindow::SetContentDocumentDisplayed(bool aDisplayed) {
  mContentDocumentDisplayed = aDisplayed;
}

bool nsWindow::IsContentDocumentDisplayed() {
  return mContentDocumentDisplayed;
}

void nsWindow::RecvToolbarAnimatorMessageFromCompositor(int32_t aMessage) {
  MOZ_ASSERT(AndroidBridge::IsJavaUiThread());
  if (::mozilla::jni::NativeWeakPtr<LayerViewSupport>::Accessor lvs{
          mLayerViewSupport.Access()}) {
    lvs->RecvToolbarAnimatorMessage(aMessage);
  }
}

static int32_t ConvertScrollUpdateSource(
    CompositorScrollUpdate::Source aSource) {
  switch (aSource) {
    case CompositorScrollUpdate::Source::UserInteraction:
      return java::GeckoSession::ScrollPositionUpdate::SOURCE_USER_INTERACTION;
    case CompositorScrollUpdate::Source::Other:
      return java::GeckoSession::ScrollPositionUpdate::SOURCE_OTHER;
  }
  MOZ_ASSERT_UNREACHABLE("Unknown CompositorScrollUpdate::Source");
  return java::GeckoSession::ScrollPositionUpdate::SOURCE_USER_INTERACTION;
}

void nsWindow::NotifyCompositorScrollUpdate(
    const CompositorScrollUpdate& aUpdate) {
  MOZ_ASSERT(AndroidBridge::IsJavaUiThread());
  if (::mozilla::jni::NativeWeakPtr<LayerViewSupport>::Accessor lvs{
          mLayerViewSupport.Access()}) {
    const auto& compositor = lvs->GetJavaCompositor();
    mContentDocumentDisplayed = true;
    compositor->NotifyCompositorScrollUpdate(
        aUpdate.mMetrics.mVisualScrollOffset.x,
        aUpdate.mMetrics.mVisualScrollOffset.y, aUpdate.mMetrics.mZoom.scale,
        ConvertScrollUpdateSource(aUpdate.mSource));
  }
}

void nsWindow::RecvScreenPixels(Shmem&& aMem, const ScreenIntSize& aSize,
                                bool aNeedsYFlip) {
  MOZ_ASSERT(AndroidBridge::IsJavaUiThread());
  if (::mozilla::jni::NativeWeakPtr<LayerViewSupport>::Accessor lvs{
          mLayerViewSupport.Access()}) {
    lvs->RecvScreenPixels(std::move(aMem), aSize, aNeedsYFlip);
  }
}

void nsWindow::UpdateDynamicToolbarMaxHeight(ScreenIntCoord aHeight) {
  if (mDynamicToolbarMaxHeight == aHeight) {
    return;
  }

  mDynamicToolbarMaxHeight = aHeight;

  if (mWidgetListener) {
    mWidgetListener->DynamicToolbarMaxHeightChanged(aHeight);
  }

  if (mAttachedWidgetListener) {
    mAttachedWidgetListener->DynamicToolbarMaxHeightChanged(aHeight);
  }
}

void nsWindow::UpdateDynamicToolbarOffset(ScreenIntCoord aOffset) {
  if (mWidgetListener) {
    mWidgetListener->DynamicToolbarOffsetChanged(aOffset);
  }

  if (mAttachedWidgetListener) {
    mAttachedWidgetListener->DynamicToolbarOffsetChanged(aOffset);
  }
}

void nsWindow::PipModeChanged(bool aPipMode) {
  if (mWidgetListener) {
    mWidgetListener->AndroidPipModeChanged(aPipMode);
  }

  if (mAttachedWidgetListener) {
    mAttachedWidgetListener->AndroidPipModeChanged(aPipMode);
  }
}

void nsWindow::KeyboardHeightChanged(ScreenIntCoord aHeight) {
  if (mWidgetListener) {
    mWidgetListener->KeyboardHeightChanged(aHeight);
  }

  if (mAttachedWidgetListener) {
    mAttachedWidgetListener->KeyboardHeightChanged(aHeight);
  }
}

LayoutDeviceIntMargin nsWindow::GetSafeAreaInsets() const {
  return mSafeAreaInsets;
}

void nsWindow::UpdateSafeAreaInsets(
    const LayoutDeviceIntMargin& aSafeAreaInsets) {
  mSafeAreaInsets = aSafeAreaInsets;

  if (mWidgetListener) {
    mWidgetListener->SafeAreaInsetsChanged(aSafeAreaInsets);
  }

  if (mAttachedWidgetListener) {
    mAttachedWidgetListener->SafeAreaInsetsChanged(aSafeAreaInsets);
  }
}

jni::NativeWeakPtr<NPZCSupport> nsWindow::GetNPZCSupportWeakPtr() {
  return mNPZCSupport;
}

already_AddRefed<nsIWidget> nsIWidget::CreateTopLevelWindow() {
  nsCOMPtr<nsIWidget> window = new nsWindow();
  return window.forget();
}

already_AddRefed<nsIWidget> nsIWidget::CreateChildWindow() {
  nsCOMPtr<nsIWidget> window = new nsWindow();
  return window.forget();
}

static already_AddRefed<DataSourceSurface> GetCursorImage(
    const nsIWidget::Cursor& aCursor, mozilla::CSSToLayoutDeviceScale aScale) {
  if (!aCursor.IsCustom()) {
    return nullptr;
  }

  RefPtr<DataSourceSurface> destDataSurface;

  nsIntSize size = nsIWidget::CustomCursorSize(aCursor);
  // prevent DoS attacks
  if (size.width > 128 || size.height > 128) {
    return nullptr;
  }

  RefPtr<gfx::SourceSurface> surface = aCursor.mContainer->GetFrameAtSize(
      size * aScale.scale, imgIContainer::FRAME_CURRENT,
      imgIContainer::FLAG_SYNC_DECODE | imgIContainer::FLAG_ASYNC_NOTIFY);
  if (NS_WARN_IF(!surface)) {
    return nullptr;
  }

  return AndroidWidgetUtils::GetDataSourceSurfaceForAndroidBitmap(surface);
}

static int32_t GetCursorType(nsCursor aCursor) {
  // When our minimal requirement of SDK version is 25+,
  // we should replace with JNI auto-generator.
  switch (aCursor) {
    case eCursor_standard:
      // android.view.PointerIcon.TYPE_ARROW
      return 0x3e8;
    case eCursor_wait:
      // android.view.PointerIcon.TYPE_WAIT
      return 0x3ec;
    case eCursor_select:
      // android.view.PointerIcon.TYPE_TEXT;
      return 0x3f0;
    case eCursor_hyperlink:
      // android.view.PointerIcon.TYPE_HAND
      return 0x3ea;
    case eCursor_n_resize:
    case eCursor_s_resize:
    case eCursor_ns_resize:
    case eCursor_row_resize:
      // android.view.PointerIcon.TYPE_VERTICAL_DOUBLE_ARROW
      return 0x3f7;
    case eCursor_w_resize:
    case eCursor_e_resize:
    case eCursor_ew_resize:
    case eCursor_col_resize:
      // android.view.PointerIcon.TYPE_HORIZONTAL_DOUBLE_ARROW
      return 0x3f6;
    case eCursor_nw_resize:
    case eCursor_se_resize:
    case eCursor_nwse_resize:
      // android.view.PointerIcon.TYPE_TOP_LEFT_DIAGONAL_DOUBLE_ARROW
      return 0x3f9;
    case eCursor_ne_resize:
    case eCursor_sw_resize:
    case eCursor_nesw_resize:
      // android.view.PointerIcon.TYPE_TOP_RIGHT_DIAGONAL_DOUBLE_ARROW
      return 0x3f8;
    case eCursor_crosshair:
      // android.view.PointerIcon.TYPE_CROSSHAIR
      return 0x3ef;
    case eCursor_move:
      // android.view.PointerIcon.TYPE_ARROW
      return 0x3e8;
    case eCursor_help:
      // android.view.PointerIcon.TYPE_HELP
      return 0x3eb;
    case eCursor_copy:
      // android.view.PointerIcon.TYPE_COPY
      return 0x3f3;
    case eCursor_alias:
      // android.view.PointerIcon.TYPE_ALIAS
      return 0x3f2;
    case eCursor_context_menu:
      // android.view.PointerIcon.TYPE_CONTEXT_MENU
      return 0x3e9;
    case eCursor_cell:
      // android.view.PointerIcon.TYPE_CELL
      return 0x3ee;
    case eCursor_grab:
      // android.view.PointerIcon.TYPE_GRAB
      return 0x3fc;
    case eCursor_grabbing:
      // android.view.PointerIcon.TYPE_GRABBING
      return 0x3fd;
    case eCursor_spinning:
      // android.view.PointerIcon.TYPE_WAIT
      return 0x3ec;
    case eCursor_zoom_in:
      // android.view.PointerIcon.TYPE_ZOOM_IN
      return 0x3fa;
    case eCursor_zoom_out:
      // android.view.PointerIcon.TYPE_ZOOM_OUT
      return 0x3fb;
    case eCursor_not_allowed:
      // android.view.PointerIcon.TYPE_NO_DROP:
      return 0x3f4;
    case eCursor_no_drop:
      // android.view.PointerIcon.TYPE_NO_DROP:
      return 0x3f4;
    case eCursor_vertical_text:
      // android.view.PointerIcon.TYPE_VERTICAL_TEXT
      return 0x3f1;
    case eCursor_all_scroll:
      // android.view.PointerIcon.TYPE_ALL_SCROLL
      return 0x3f5;
    case eCursor_none:
      // android.view.PointerIcon.TYPE_NULL
      return 0;
    default:
      NS_WARNING_ASSERTION(aCursor, "Invalid cursor type");
      // android.view.PointerIcon.TYPE_ARROW
      return 0x3e8;
  }
}

void nsWindow::SetCursor(const Cursor& aCursor) {
  if (mozilla::jni::GetAPIVersion() < 24) {
    return;
  }

  // Only change cursor if it's actually been changed
  if (!mUpdateCursor && mCursor == aCursor) {
    return;
  }

  mUpdateCursor = false;
  mCursor = aCursor;

  int32_t type = 0;
  RefPtr<DataSourceSurface> destDataSurface =
      GetCursorImage(aCursor, GetDefaultScale());
  if (!destDataSurface) {
    type = GetCursorType(aCursor.mDefaultCursor);
  }

  if (mozilla::jni::NativeWeakPtr<LayerViewSupport>::Accessor lvs{
          mLayerViewSupport.Access()}) {
    const auto& compositor = lvs->GetJavaCompositor();

    DispatchToUiThread(
        "nsWindow::SetCursor",
        [compositor = GeckoSession::Compositor::GlobalRef(compositor), type,
         destDataSurface = std::move(destDataSurface),
         hotspotX = aCursor.mHotspotX, hotspotY = aCursor.mHotspotY] {
          java::sdk::Bitmap::LocalRef bitmap;
          if (destDataSurface) {
            DataSourceSurface::ScopedMap destMap(destDataSurface,
                                                 DataSourceSurface::READ);
            auto pixels = mozilla::jni::ByteBuffer::New(
                reinterpret_cast<int8_t*>(destMap.GetData()),
                destMap.GetStride() * destDataSurface->GetSize().height);
            bitmap = java::sdk::Bitmap::CreateBitmap(
                destDataSurface->GetSize().width,
                destDataSurface->GetSize().height,
                java::sdk::Bitmap::Config::ARGB_8888());
            bitmap->CopyPixelsFromBuffer(pixels);
          }
          compositor->SetPointerIcon(type, bitmap, hotspotX, hotspotY);
        });
  }
}
