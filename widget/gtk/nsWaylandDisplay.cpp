/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWaylandDisplay.h"

#include <dlfcn.h>

#include "DMABufFormats.h"
#include "ScreenHelperGTK.h"
#include "WidgetUtilsGtk.h"
#include "base/message_loop.h"  // for MessageLoop
#include "base/task.h"          // for NewRunnableMethod, etc
#include "mozilla/Sprintf.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPrefs_general.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/gfx/Logging.h"  // for gfxCriticalNote
#include "nsGtkKeyUtils.h"
#include "nsGtkUtils.h"
#include "nsIAppStartup.h"
#include "nsLayoutUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nsWindow.h"
#include "wayland-proxy.h"

#undef LOG
#undef LOG_VERBOSE
#ifdef MOZ_LOGGING
#  include "Units.h"
#  include "mozilla/Logging.h"
#  include "nsTArray.h"
extern mozilla::LazyLogModule gWidgetWaylandLog;
#  define LOG(...) \
    MOZ_LOG(gWidgetWaylandLog, mozilla::LogLevel::Debug, (__VA_ARGS__))
#  define LOG_VERBOSE(...) \
    MOZ_LOG(gWidgetWaylandLog, mozilla::LogLevel::Verbose, (__VA_ARGS__))
#else
#  define LOG(...)
#  define LOG_VERBOSE(...)
#endif /* MOZ_LOGGING */

namespace mozilla::widget {

static nsWaylandDisplay* gWaylandDisplay;

void WaylandDisplayRelease() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread(),
                     "WaylandDisplay can be released in main thread only!");
  if (!gWaylandDisplay) {
    return;
  }
  LOG("WaylandDisplayRelease()");
  delete gWaylandDisplay;
  gWaylandDisplay = nullptr;
}

wl_display* WaylandDisplayGetWLDisplay() {
  GdkDisplay* disp = gdk_display_get_default();
  if (!GdkIsWaylandDisplay(disp)) {
    return nullptr;
  }
  return gdk_wayland_display_get_wl_display(disp);
}

nsWaylandDisplay* WaylandDisplayGet() {
  if (!gWaylandDisplay) {
    MOZ_RELEASE_ASSERT(NS_IsMainThread(),
                       "WaylandDisplay can be created in main thread only!");
    wl_display* waylandDisplay = WaylandDisplayGetWLDisplay();
    if (!waylandDisplay) {
      return nullptr;
    }
    // We're setting Wayland client buffer size here (i.e. our write buffer).
    // Server buffer size is set by compositor and we may use the same buffer
    // sizes on both sides. Mutter uses 1024 * 1024 (1M) so let's use the same
    // value.
    wl_display_set_max_buffer_size(waylandDisplay, 1024 * 1024);
    gWaylandDisplay = new nsWaylandDisplay(waylandDisplay);
    gWaylandDisplay->Init();
  }
  return gWaylandDisplay;
}

#ifdef MOZ_LOGGING
void nsWaylandDisplay::SetShmFormat(uint32_t aFormat) {
  switch (aFormat) {
    case WL_SHM_FORMAT_ABGR2101010:
      LOG("WL_SHM_FORMAT_ABGR2101010 supported");
      break;
    case WL_SHM_FORMAT_YUV420:
      LOG("WL_SHM_FORMAT_YUV420 supported");
      break;
  }
}
#endif

void nsWaylandDisplay::SetShm(wl_shm* aShm) {
  mShm = aShm;
#ifdef MOZ_LOGGING
  static const struct wl_shm_listener shm_listener{
      [](void* data, struct wl_shm* wl_shm, uint32_t format) {
        auto* display = static_cast<nsWaylandDisplay*>(data);
        display->SetShmFormat(format);
      }};
  wl_shm_add_listener(mShm, &shm_listener, this);
#endif
}

class WaylandPointerEvent {
 public:
  constexpr WaylandPointerEvent() = default;

  RefPtr<nsWindow> TakeWindow(wl_surface* aSurface) {
    if (!aSurface) {
      mWindow = nullptr;
    } else {
      GdkWindow* window =
          static_cast<GdkWindow*>(wl_surface_get_user_data(aSurface));
      mWindow = window ? static_cast<nsWindow*>(
                             g_object_get_data(G_OBJECT(window), "nsWindow"))
                       : nullptr;
    }
    return mWindow;
  }
  already_AddRefed<nsWindow> GetAndClearWindow() { return mWindow.forget(); }
  RefPtr<nsWindow> GetWindow() { return mWindow; }

  void SetSource(int32_t aSource) { mSource = aSource; }

  void SetDelta120(uint32_t aAxis, int32_t aDelta) {
    switch (aAxis) {
      case WL_POINTER_AXIS_VERTICAL_SCROLL:
        mDeltaY = aDelta / 120.0f;
        break;
      case WL_POINTER_AXIS_HORIZONTAL_SCROLL:
        mDeltaX = aDelta / 120.0f;
        break;
      default:
        NS_WARNING("WaylandPointerEvent::SetDelta120(): wrong axis!");
        break;
    }
  }

  void SetTime(uint32_t aTime) { mTime = aTime; }

  void SendScrollEvent() {
    if (!mWindow || !nsLayoutUtils::IsSmoothScrollingEnabled()) {
      return;
    }

    // nsWindow::OnSmoothScrollEvent() may spin event loop so
    // mWindow/mSource/delta may be replaced.
    int32_t source = mSource;
    float deltaX = mDeltaX;
    float deltaY = mDeltaY;

    mSource = -1;
    mDeltaX = mDeltaY = 0.0f;

    // We process wheel events only now.
    if (source != WL_POINTER_AXIS_SOURCE_WHEEL) {
      return;
    }

    RefPtr<nsWindow> win = mWindow;
    uint32_t eventTime = mTime;
    win->OnSmoothScrollEvent(eventTime, deltaX, deltaY);
  }

  void Clear() { mWindow = nullptr; }

 private:
  StaticRefPtr<nsWindow> mWindow;
  uint32_t mTime = 0;
  int32_t mSource = 0;
  float mDeltaX = 0;
  float mDeltaY = 0;
};

static WaylandPointerEvent sHoldGesture;

static void gesture_hold_begin(void* data,
                               struct zwp_pointer_gesture_hold_v1* hold,
                               uint32_t serial, uint32_t time,
                               struct wl_surface* surface, uint32_t fingers) {
  RefPtr<nsWindow> window = sHoldGesture.TakeWindow(surface);
  if (!window) {
    return;
  }
  window->OnTouchpadHoldEvent(GDK_TOUCHPAD_GESTURE_PHASE_BEGIN, time, fingers);
}

static void gesture_hold_end(void* data,
                             struct zwp_pointer_gesture_hold_v1* hold,
                             uint32_t serial, uint32_t time,
                             int32_t cancelled) {
  RefPtr<nsWindow> window = sHoldGesture.GetAndClearWindow();
  if (!window) {
    return;
  }
  window->OnTouchpadHoldEvent(cancelled ? GDK_TOUCHPAD_GESTURE_PHASE_CANCEL
                                        : GDK_TOUCHPAD_GESTURE_PHASE_END,
                              time, 0);
}

static const struct zwp_pointer_gesture_hold_v1_listener gesture_hold_listener =
    {gesture_hold_begin, gesture_hold_end};

static WaylandPointerEvent sScrollEvent;

uint32_t gLastSerial = 0;
uint32_t nsWaylandDisplay::GetLastEventSerial() { return gLastSerial; }

static void pointer_handle_enter(void* data, struct wl_pointer* pointer,
                                 uint32_t serial, struct wl_surface* surface,
                                 wl_fixed_t sx, wl_fixed_t sy) {
  sScrollEvent.TakeWindow(surface);
}

static void pointer_handle_leave(void* data, struct wl_pointer* pointer,
                                 uint32_t serial, struct wl_surface* surface) {
  sScrollEvent.Clear();
}

static void pointer_handle_motion(void* data, struct wl_pointer* pointer,
                                  uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
}

static void pointer_handle_button(void* data, struct wl_pointer* pointer,
                                  uint32_t serial, uint32_t time,
                                  uint32_t button, uint32_t state) {
  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    gLastSerial = serial;
  }
}

static void pointer_handle_axis(void* data, struct wl_pointer* pointer,
                                uint32_t time, uint32_t axis,
                                wl_fixed_t value) {
  sScrollEvent.SetTime(time);
}

static void pointer_handle_frame(void* data, struct wl_pointer* pointer) {
  sScrollEvent.SendScrollEvent();
}

static void pointer_handle_axis_source(
    void* data, struct wl_pointer* pointer,
    /*enum wl_pointer_axis_source */ uint32_t source) {
  sScrollEvent.SetSource(source);
}

static void pointer_handle_axis_stop(void* data, struct wl_pointer* pointer,
                                     uint32_t time, uint32_t axis) {}

static void pointer_handle_axis_discrete(void* data, struct wl_pointer* pointer,
                                         uint32_t axis, int32_t value) {}

static void pointer_handle_axis_value120(void* data, struct wl_pointer* pointer,
                                         uint32_t axis, int32_t value) {
  sScrollEvent.SetDelta120(axis, value);
}

/*
 * Example of scroll events we get for various devices. Note that
 * even three different devices has the same wl_pointer.
 *
 * Standard mouse wheel:
 *
 *  pointer_handle_axis_source pointer 0x7fd14fd4bac0 source 0
 *  pointer_handle_axis_value120 pointer 0x7fd14fd4bac0 value 120
 *  pointer_handle_axis pointer 0x7fd14fd4bac0 time 9470441 value 10.000000
 *  pointer_handle_frame
 *
 * Hi-res mouse wheel:
 *
 * pointer_handle_axis_source pointer 0x7fd14fd4bac0 source 0
 * pointer_handle_axis_value120 pointer 0x7fd14fd4bac0 value -24
 * pointer_handle_axis pointer 0x7fd14fd4bac0 time 9593205 value -1.992188
 * pointer_handle_frame
 *
 * Touchpad:
 *
 * pointer_handle_axis_source pointer 0x7fd14fd4bac0 source 1
 * pointer_handle_axis pointer 0x7fd14fd4bac0 time 9431830 value 0.312500
 * pointer_handle_axis pointer 0x7fd14fd4bac0 time 9431830 value -1.015625
 * pointer_handle_frame
 */

static const struct moz_wl_pointer_listener pointer_listener = {
    pointer_handle_enter,         pointer_handle_leave,
    pointer_handle_motion,        pointer_handle_button,
    pointer_handle_axis,          pointer_handle_frame,
    pointer_handle_axis_source,   pointer_handle_axis_stop,
    pointer_handle_axis_discrete, pointer_handle_axis_value120,
};

void nsWaylandDisplay::SetPointer(wl_pointer* aPointer) {
  // Don't even try on such old interface
  if (wl_proxy_get_version((struct wl_proxy*)aPointer) <
      WL_POINTER_RELEASE_SINCE_VERSION) {
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(!mPointer);
  mPointer = aPointer;

  // We're interested in pointer_handle_axis_value120() only for now.
  if (wl_proxy_get_version((struct wl_proxy*)aPointer) >=
      WL_POINTER_AXIS_VALUE120_SINCE_VERSION) {
    wl_pointer_add_listener(
        mPointer, (const wl_pointer_listener*)&pointer_listener, this);
  }

  // mPointerGestures is set by zwp_pointer_gestures_v1 if we have it.
  if (mPointerGestures) {
    mPointerGestureHold =
        zwp_pointer_gestures_v1_get_hold_gesture(mPointerGestures, mPointer);
    zwp_pointer_gesture_hold_v1_set_user_data(mPointerGestureHold, this);
    zwp_pointer_gesture_hold_v1_add_listener(mPointerGestureHold,
                                             &gesture_hold_listener, this);
  }
}

void nsWaylandDisplay::RemovePointer() {
  wl_pointer_release(mPointer);
  mPointer = nullptr;
}

static void seat_handle_capabilities(void* data, struct wl_seat* seat,
                                     unsigned int caps) {
  auto* display = static_cast<nsWaylandDisplay*>(data);
  if (!display) {
    return;
  }

  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !display->GetPointer()) {
    display->SetPointer(wl_seat_get_pointer(seat));
  } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && display->GetPointer()) {
    display->RemovePointer();
  }

  wl_keyboard* keyboard = display->GetKeyboard();
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard) {
    display->SetKeyboard(wl_seat_get_keyboard(seat));
  } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && keyboard) {
    display->ClearKeyboard();
  }

  wl_touch* touch = display->GetTouch();
  if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !touch) {
    display->SetTouch(wl_seat_get_touch(seat));
  } else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && touch) {
    display->ClearTouch();
  }
}

static void seat_handle_name(void* data, struct wl_seat* seat,
                             const char* name) {
  /* We don't care about the name. */
}

static const struct wl_seat_listener seat_listener = {
    seat_handle_capabilities,
    seat_handle_name,
};

void nsWaylandDisplay::SetSeat(wl_seat* aSeat, int aSeatId) {
  mSeat = aSeat;
  mSeatId = aSeatId;
  wl_seat_add_listener(aSeat, &seat_listener, this);
}

void nsWaylandDisplay::RemoveSeat(int aSeatId) {
  if (mSeatId == aSeatId) {
    mSeat = nullptr;
    mSeatId = -1;
  }
}

/* This keymap routine is derived from weston-2.0.0/clients/simple-im.c
 */
static void keyboard_handle_keymap(void* data, struct wl_keyboard* wl_keyboard,
                                   uint32_t format, int fd, uint32_t size) {
  KeymapWrapper::HandleKeymap(format, fd, size);
}

static void keyboard_handle_enter(void* data, struct wl_keyboard* keyboard,
                                  uint32_t serial, struct wl_surface* surface,
                                  struct wl_array* keys) {
  gLastSerial = serial;
}

static void keyboard_handle_leave(void* data, struct wl_keyboard* keyboard,
                                  uint32_t serial, struct wl_surface* surface) {
  KeymapWrapper::ResetRepeatState();
}

static void keyboard_handle_key(void* data, struct wl_keyboard* keyboard,
                                uint32_t serial, uint32_t time, uint32_t key,
                                uint32_t state) {
  if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    gLastSerial = serial;
  }
  // hardware key code is +8.
  // https://gitlab.gnome.org/GNOME/gtk/-/blob/3.24.41/gdk/wayland/gdkdevice-wayland.c#L2341
  KeymapWrapper::KeyboardHandlerForWayland(serial, key + 8, state);
}

static void keyboard_handle_modifiers(void* data, struct wl_keyboard* keyboard,
                                      uint32_t serial, uint32_t mods_depressed,
                                      uint32_t mods_latched,
                                      uint32_t mods_locked, uint32_t group) {}
static void keyboard_handle_repeat_info(void* data,
                                        struct wl_keyboard* keyboard,
                                        int32_t rate, int32_t delay) {}

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_handle_keymap,    keyboard_handle_enter,
    keyboard_handle_leave,     keyboard_handle_key,
    keyboard_handle_modifiers, keyboard_handle_repeat_info};

void nsWaylandDisplay::SetKeyboard(wl_keyboard* aKeyboard) {
  MOZ_ASSERT(aKeyboard);
  MOZ_DIAGNOSTIC_ASSERT(!mKeyboard);
  mKeyboard = aKeyboard;
  wl_keyboard_add_listener(mKeyboard, &keyboard_listener, nullptr);
}

void nsWaylandDisplay::ClearKeyboard() {
  if (mKeyboard) {
    wl_keyboard_release(mKeyboard);
    mKeyboard = nullptr;
    KeymapWrapper::ClearKeymap();
  }
}

static void touch_handle_down(void* data, struct wl_touch* touch,
                              uint32_t serial, uint32_t time,
                              struct wl_surface* surface, int32_t id,
                              wl_fixed_t x, wl_fixed_t y) {
  gLastSerial = serial;
}

static void touch_handle_up(void* data, struct wl_touch* touch, uint32_t serial,
                            uint32_t time, int32_t id) {}

static void touch_handle_motion(void* data, struct wl_touch* touch,
                                uint32_t time, int32_t id, wl_fixed_t x,
                                wl_fixed_t y) {}

static void touch_handle_frame(void* data, struct wl_touch* touch) {}

static void touch_handle_cancel(void* data, struct wl_touch* touch) {}

static const struct wl_touch_listener touch_listener = {
    touch_handle_down,  touch_handle_up,     touch_handle_motion,
    touch_handle_frame, touch_handle_cancel,
};

void nsWaylandDisplay::SetTouch(wl_touch* aTouch) {
  MOZ_ASSERT(aTouch);
  MOZ_DIAGNOSTIC_ASSERT(!mTouch);
  mTouch = aTouch;
  wl_touch_add_listener(mTouch, &touch_listener, nullptr);
}

void nsWaylandDisplay::ClearTouch() {
  MozClearPointer(mTouch, wl_touch_release);
}

void nsWaylandDisplay::SetCompositor(wl_compositor* aCompositor) {
  mCompositor = aCompositor;
}

void nsWaylandDisplay::SetSubcompositor(wl_subcompositor* aSubcompositor) {
  mSubcompositor = aSubcompositor;
}

void nsWaylandDisplay::SetDataDeviceManager(
    wl_data_device_manager* aDataDeviceManager) {
  mDataDeviceManager = aDataDeviceManager;
}

void nsWaylandDisplay::SetPrimarySelectionDeviceManager(
    gtk_primary_selection_device_manager* aPrimarySelectionDeviceManager) {
  mPrimarySelectionDeviceManagerGtk = aPrimarySelectionDeviceManager;
  mIsPrimarySelectionEnabled = !!mPrimarySelectionDeviceManagerGtk;
}

void nsWaylandDisplay::SetPrimarySelectionDeviceManager(
    zwp_primary_selection_device_manager_v1* aPrimarySelectionDeviceManager) {
  mPrimarySelectionDeviceManagerZwpV1 = aPrimarySelectionDeviceManager;
  mIsPrimarySelectionEnabled = !!mPrimarySelectionDeviceManagerZwpV1;
}

void nsWaylandDisplay::SetIdleInhibitManager(
    zwp_idle_inhibit_manager_v1* aIdleInhibitManager) {
  mIdleInhibitManager = aIdleInhibitManager;
}

void nsWaylandDisplay::SetViewporter(wp_viewporter* aViewporter) {
  mViewporter = aViewporter;
}

void nsWaylandDisplay::SetRelativePointerManager(
    zwp_relative_pointer_manager_v1* aRelativePointerManager) {
  mRelativePointerManager = aRelativePointerManager;
}

void nsWaylandDisplay::SetPointerConstraints(
    zwp_pointer_constraints_v1* aPointerConstraints) {
  mPointerConstraints = aPointerConstraints;
}

void nsWaylandDisplay::SetPointerGestures(
    zwp_pointer_gestures_v1* aPointerGestures) {
  mPointerGestures = aPointerGestures;
}

void nsWaylandDisplay::SetDmabuf(zwp_linux_dmabuf_v1* aDmabuf, int aVersion) {
  if (!aDmabuf || aVersion < ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION) {
    return;
  }
  mDmabuf = aDmabuf;
  mDmabufIsFeedback =
      (aVersion >= ZWP_LINUX_DMABUF_V1_GET_DEFAULT_FEEDBACK_SINCE_VERSION);
  if (mDmabufIsFeedback) {
    mFormats->InitFeedback(mDmabuf, nullptr);
  } else {
    mFormats->InitV3(mDmabuf);
  }
}

void nsWaylandDisplay::EnsureDMABufFormats() {
  if (mDmabuf && !mDmabufIsFeedback) {
    mFormats->InitV3Done();
  }
  mFormats->EnsureBasicFormats();
}

void nsWaylandDisplay::SetXdgActivation(xdg_activation_v1* aXdgActivation) {
  mXdgActivation = aXdgActivation;
}

void nsWaylandDisplay::SetAppMenuManager(
    org_kde_kwin_appmenu_manager* aAppMenuManager) {
  mAppMenuManager = aAppMenuManager;
}

void nsWaylandDisplay::SetFixes(wl_fixes* aFixes) { mFixes = aFixes; }

// BT2020/PQ is the minimum to enable HDR.
bool nsWaylandDisplay::IsHDREnabled() const {
  return mColorManagerSupportedFeature.mParametric &&
         mSupportedPrimaries[WP_COLOR_MANAGER_V1_PRIMARIES_BT2020] &&
         mSupportedTransfer[WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ];
}

void nsWaylandDisplay::SetCMSupportedFeature(uint32_t aFeature) {
  LOG("nsWaylandDisplay::SetCMSupportedFeature() [%d]", aFeature);
  switch (aFeature) {
    case WP_COLOR_MANAGER_V1_FEATURE_ICC_V2_V4:
      mColorManagerSupportedFeature.mICC = true;
      break;
    case WP_COLOR_MANAGER_V1_FEATURE_PARAMETRIC:
      mColorManagerSupportedFeature.mParametric = true;
      break;
    case WP_COLOR_MANAGER_V1_FEATURE_SET_PRIMARIES:
      mColorManagerSupportedFeature.mPrimaries = true;
      break;
    case WP_COLOR_MANAGER_V1_FEATURE_SET_TF_POWER:
      mColorManagerSupportedFeature.mFTPower = true;
      break;
    case WP_COLOR_MANAGER_V1_FEATURE_SET_LUMINANCES:
      mColorManagerSupportedFeature.mLuminances = true;
      break;
    case WP_COLOR_MANAGER_V1_FEATURE_SET_MASTERING_DISPLAY_PRIMARIES:
      mColorManagerSupportedFeature.mDisplayPrimaries = true;
      break;
  }
}

void nsWaylandDisplay::SetCMSupportedTFNamed(uint32_t aTF) {
  if (aTF < sColorTransfersNum) {
    LOG("nsWaylandDisplay::SetCMSupportedTFNamed() [%d]", aTF);
    mSupportedTransfer[aTF] = aTF;
  } else {
    NS_WARNING("Unknow color transfer function!");
  }
}

void nsWaylandDisplay::SetCMSupportedPrimariesNamed(uint32_t aPrimaries) {
  if (aPrimaries < sColorPrimariesNum) {
    LOG("nsWaylandDisplay::SetCMSupportedPrimariesNamed() [%u]", aPrimaries);
    mSupportedPrimaries[aPrimaries] = aPrimaries;
  } else {
    NS_WARNING("Unknown color primaries!");
  }
}

static void supported_intent(void* data,
                             struct wp_color_manager_v1* color_manager,
                             uint32_t render_intent) {}

static void supported_feature(void* data,
                              struct wp_color_manager_v1* color_manager,
                              uint32_t feature) {
  auto* display = static_cast<nsWaylandDisplay*>(data);
  display->SetCMSupportedFeature(feature);
}

static void supported_tf_named(void* data,
                               struct wp_color_manager_v1* color_manager,
                               uint32_t tf) {
  auto* display = static_cast<nsWaylandDisplay*>(data);
  display->SetCMSupportedTFNamed(tf);
}

static void supported_primaries_named(void* data,
                                      struct wp_color_manager_v1* color_manager,
                                      uint32_t primaries) {
  auto* display = static_cast<nsWaylandDisplay*>(data);
  display->SetCMSupportedPrimariesNamed(primaries);
}

static void supported_done(void* data,
                           struct wp_color_manager_v1* wp_color_manager_v1) {}

static const struct wp_color_manager_v1_listener color_manager_listener = {
    supported_intent,          supported_feature, supported_tf_named,
    supported_primaries_named, supported_done,
};

void nsWaylandDisplay::SetColorManager(wp_color_manager_v1* aColorManager) {
  mColorManager = aColorManager;
  if (mColorManager) {
    LOG("nsWaylandDisplay::SetColorManager()");
    wp_color_manager_v1_add_listener(mColorManager, &color_manager_listener,
                                     this);
  }
}

void nsWaylandDisplay::SetSupportedCoefficientsAndRanges(uint32_t aCoefficients,
                                                         uint32_t aRange) {
  if (aCoefficients < sSupportedRangesNum) {
    LOG("nsWaylandDisplay::SetSupportedCoefficientsAndRanges(): coefficients "
        "%d range %d",
        aCoefficients, aRange);
    mSupportedRanges[aCoefficients] += aRange;
  }
}

uint32_t nsWaylandDisplay::GetColorRange(uint32_t aCoefficients,
                                         bool aFullRange) {
  if (aCoefficients >= sSupportedRangesNum) {
    return 0;
  }
  auto range = mSupportedRanges[aCoefficients];
  if (aFullRange) {
    return range == sSupportedRangeBoth || range == sSupportedRangeFull
               ? WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_FULL
               : 0;
  } else {
    return range == sSupportedRangeBoth || range == sSupportedRangeLimited
               ? WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED
               : 0;
  }
}

static void supported_alpha_mode(
    void* data,
    struct wp_color_representation_manager_v1* color_representation_manager,
    uint32_t alpha_mode) {}

static void supported_coefficients_and_ranges(
    void* data,
    struct wp_color_representation_manager_v1* color_representation_manager,
    uint32_t coefficients, uint32_t range) {
  auto* display = static_cast<nsWaylandDisplay*>(data);
  display->SetSupportedCoefficientsAndRanges(coefficients, range);
}

static void color_representation_done(
    void* data,
    struct wp_color_representation_manager_v1* color_representation_manager) {}

static const struct wp_color_representation_manager_v1_listener
    color_representation_listener = {
        supported_alpha_mode,
        supported_coefficients_and_ranges,
        color_representation_done,
};

void nsWaylandDisplay::SetColorRepresentationManager(
    wp_color_representation_manager_v1* aColorRepresentationManager) {
  mColorRepresentationManager = aColorRepresentationManager;
  if (mColorRepresentationManager) {
    LOG("nsWaylandDisplay::SetColorRepresentationManager()");
    wp_color_representation_manager_v1_add_listener(
        mColorRepresentationManager, &color_representation_listener, this);
  }
}

static void output_handle_geometry(void* data, struct wl_output* wl_output,
                                   int x, int y, int physical_width,
                                   int physical_height, int subpixel,
                                   const char* make, const char* model,
                                   int32_t transform) {
  auto* monitor = static_cast<nsWaylandDisplay::MonitorConfig*>(data);
  LOG("nsWaylandDisplay ID %d geometry position %d %d physical size %d %d "
      "subpixel %d transform %d",
      monitor->id, x, y, physical_width, physical_height, subpixel, transform);
  monitor->x = x;
  monitor->y = y;
  monitor->pendingChanges = true;
}

static void output_handle_mode(void* data, struct wl_output* wl_output,
                               uint32_t flags, int width, int height,
                               int refresh) {
  auto* monitor = static_cast<nsWaylandDisplay::MonitorConfig*>(data);
  if ((flags & WL_OUTPUT_MODE_CURRENT) == 0) {
    return;
  }
  LOG("nsWaylandDisplay ID %d mode output size %d x %d", monitor->id, width,
      height);
  monitor->pixelWidth = width;
  monitor->pixelHeight = height;
  monitor->pendingChanges = true;
}

static void output_handle_scale(void* data, struct wl_output* wl_output,
                                int32_t scale) {
  auto* monitor = static_cast<nsWaylandDisplay::MonitorConfig*>(data);
  LOG("nsWaylandDisplay ID %d Scale change [%d]", monitor->id, scale);
  monitor->pendingChanges = true;
}

static void output_handle_done(void* data, struct wl_output* wl_output) {
  auto* monitor = static_cast<nsWaylandDisplay::MonitorConfig*>(data);
  LOG("nsWaylandDisplay ID %d Done", monitor->id);
  monitor->pendingChanges = false;

  WaylandDisplayGet()->RefreshScreens();
}

static const struct wl_output_listener output_listener = {
    output_handle_geometry,
    output_handle_mode,
    output_handle_done,
    output_handle_scale,
};

void nsWaylandDisplay::AddMonitorConfig(int aId, wl_output* aWlOutput) {
  LOG("nsWaylandDisplay add monitor ID %d num %zu", aId, mMonitors.Length());
  mMonitors.AppendElement(std::make_unique<MonitorConfig>(aId, aWlOutput));
}

nsWaylandDisplay::MonitorConfig::MonitorConfig(int aId,
                                               struct wl_output* aWlOutput)
    : id(aId), wlOutput(aWlOutput) {
  LOG("MonitorConfig() add ID %d", id);
  wl_output_add_listener(wlOutput, &output_listener, this);
}

nsWaylandDisplay::MonitorConfig::~MonitorConfig() {
  LOG("~MonitorConfig() delete ID %d", id);
  MozClearPointer(wlOutput, wl_output_release);
}

bool nsWaylandDisplay::RemoveMonitorConfig(int aId) {
  for (unsigned int i = 0; i < mMonitors.Length(); i++) {
    if (mMonitors[i]->id == aId) {
      LOG("nsWaylandDisplay remove Monitor ID %d num %d", aId, i);
      mMonitors.RemoveElementAt(i);
      return true;
    }
  }
  return false;
}

nsWaylandDisplay::MonitorConfig* nsWaylandDisplay::GetMonitorConfig(int x,
                                                                    int y) {
  for (const auto& monitor : mMonitors) {
    if (monitor->x == x && monitor->y == y) {
      LOG("nsWaylandDisplay::GetMonitorConfig() %d, %d matches", x, y);
      return monitor.get();
    }
  }
  LOG("nsWaylandDisplay::GetMonitorConfig() %d, %d missing!", x, y);
  return nullptr;
}

void nsWaylandDisplay::RefreshScreens() {
  LOG("nsWaylandDisplay::RefreshScreens()");
  for (unsigned int i = 0; i < mMonitors.Length(); i++) {
    if (mMonitors[i]->pendingChanges) {
      LOG("  monitor ID %d is not complete", mMonitors[i]->id);
      return;
    }
  }
  ScreenHelperGTK::RequestRefreshScreens();
}

static void global_registry_handler(void* data, wl_registry* registry,
                                    uint32_t id, const char* interface,
                                    uint32_t version) {
  auto* display = static_cast<nsWaylandDisplay*>(data);
  if (!display) {
    return;
  }

  nsDependentCString iface(interface);
  if (iface.EqualsLiteral("wl_shm")) {
    auto* shm = WaylandRegistryBind<wl_shm>(registry, id, &wl_shm_interface, 1);
    display->SetShm(shm);
  } else if (strcmp(interface, "wl_data_device_manager") == 0) {
    int data_device_manager_version = MIN(version, 3);
    auto* data_device_manager = WaylandRegistryBind<wl_data_device_manager>(
        registry, id, &wl_data_device_manager_interface,
        data_device_manager_version);
    display->SetDataDeviceManager(data_device_manager);
  } else if (strcmp(interface, "gtk_primary_selection_device_manager") == 0) {
    auto* primary_selection_device_manager =
        WaylandRegistryBind<gtk_primary_selection_device_manager>(
            registry, id, &gtk_primary_selection_device_manager_interface, 1);
    display->SetPrimarySelectionDeviceManager(primary_selection_device_manager);
  } else if (strcmp(interface, "zwp_primary_selection_device_manager_v1") ==
             0) {
    auto* primary_selection_device_manager =
        WaylandRegistryBind<zwp_primary_selection_device_manager_v1>(
            registry, id, &zwp_primary_selection_device_manager_v1_interface,
            1);
    display->SetPrimarySelectionDeviceManager(primary_selection_device_manager);
  } else if (iface.EqualsLiteral("zwp_idle_inhibit_manager_v1")) {
    auto* idle_inhibit_manager =
        WaylandRegistryBind<zwp_idle_inhibit_manager_v1>(
            registry, id, &zwp_idle_inhibit_manager_v1_interface, 1);
    display->SetIdleInhibitManager(idle_inhibit_manager);
  } else if (iface.EqualsLiteral("zwp_relative_pointer_manager_v1")) {
    auto* relative_pointer_manager =
        WaylandRegistryBind<zwp_relative_pointer_manager_v1>(
            registry, id, &zwp_relative_pointer_manager_v1_interface, 1);
    display->SetRelativePointerManager(relative_pointer_manager);
  } else if (iface.EqualsLiteral("zwp_pointer_constraints_v1")) {
    auto* pointer_constraints = WaylandRegistryBind<zwp_pointer_constraints_v1>(
        registry, id, &zwp_pointer_constraints_v1_interface, 1);
    display->SetPointerConstraints(pointer_constraints);
  } else if (iface.EqualsLiteral("wl_compositor") &&
             version >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION) {
    auto* compositor = WaylandRegistryBind<wl_compositor>(
        registry, id, &wl_compositor_interface,
        WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION);
    display->SetCompositor(compositor);
  } else if (iface.EqualsLiteral("wl_subcompositor")) {
    auto* subcompositor = WaylandRegistryBind<wl_subcompositor>(
        registry, id, &wl_subcompositor_interface, 1);
    display->SetSubcompositor(subcompositor);
  } else if (iface.EqualsLiteral("wp_viewporter")) {
    auto* viewporter = WaylandRegistryBind<wp_viewporter>(
        registry, id, &wp_viewporter_interface, 1);
    display->SetViewporter(viewporter);
  } else if (iface.EqualsLiteral("zwp_linux_dmabuf_v1")) {
    if (version < ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION) {
      return;
    }
    int vers =
        MIN(version, ZWP_LINUX_DMABUF_V1_GET_DEFAULT_FEEDBACK_SINCE_VERSION);
    auto* dmabuf = WaylandRegistryBind<zwp_linux_dmabuf_v1>(
        registry, id, &zwp_linux_dmabuf_v1_interface, vers);
    display->SetDmabuf(dmabuf, vers);
  } else if (iface.EqualsLiteral("xdg_session_manager_v1")) {
    auto* sessionManager = WaylandRegistryBind<xdg_session_manager_v1>(
        registry, id, &xdg_session_manager_v1_interface, 1);
    display->SetSessionManager(sessionManager);
  } else if (iface.EqualsLiteral("xdg_activation_v1")) {
    auto* activation = WaylandRegistryBind<xdg_activation_v1>(
        registry, id, &xdg_activation_v1_interface, 1);
    display->SetXdgActivation(activation);
  } else if (iface.EqualsLiteral("org_kde_kwin_appmenu_manager")) {
    auto* appMenuManager = WaylandRegistryBind<org_kde_kwin_appmenu_manager>(
        registry, id, &org_kde_kwin_appmenu_manager_interface, MIN(version, 2));
    display->SetAppMenuManager(appMenuManager);
  } else if (iface.EqualsLiteral("wl_seat") &&
             version >= WL_POINTER_RELEASE_SINCE_VERSION) {
    auto* seat = WaylandRegistryBind<wl_seat>(
        registry, id, &wl_seat_interface,
        MIN(version, WL_POINTER_AXIS_VALUE120_SINCE_VERSION));
    display->SetSeat(seat, id);
  } else if (iface.EqualsLiteral("wp_fractional_scale_manager_v1")) {
    auto* manager = WaylandRegistryBind<wp_fractional_scale_manager_v1>(
        registry, id, &wp_fractional_scale_manager_v1_interface, 1);
    display->SetFractionalScaleManager(manager);
  } else if (iface.EqualsLiteral("xx_fractional_scale_manager_v2")) {
    auto* manager = WaylandRegistryBind<xx_fractional_scale_manager_v2>(
        registry, id, &xx_fractional_scale_manager_v2_interface, 1);
    display->SetFractionalScaleManagerV2(manager);
  } else if (iface.EqualsLiteral("zwp_pointer_gestures_v1") &&
             version >=
                 ZWP_POINTER_GESTURES_V1_GET_HOLD_GESTURE_SINCE_VERSION) {
    auto* gestures = WaylandRegistryBind<zwp_pointer_gestures_v1>(
        registry, id, &zwp_pointer_gestures_v1_interface,
        ZWP_POINTER_GESTURES_V1_GET_HOLD_GESTURE_SINCE_VERSION);
    display->SetPointerGestures(gestures);
  } else if (iface.EqualsLiteral("wp_color_manager_v1")) {
    auto* colorManager = WaylandRegistryBind<wp_color_manager_v1>(
        registry, id, &wp_color_manager_v1_interface, 1);
    display->SetColorManager(colorManager);
  } else if (iface.EqualsLiteral("wp_color_representation_manager_v1")) {
    auto* colorRepresentationManager =
        WaylandRegistryBind<wp_color_representation_manager_v1>(
            registry, id, &wp_color_representation_manager_v1_interface, 1);
    display->SetColorRepresentationManager(colorRepresentationManager);
  } else if (iface.EqualsLiteral("xx_pip_shell_v1")) {
    auto* pipShell = WaylandRegistryBind<xx_pip_shell_v1>(
        registry, id, &xx_pip_shell_v1_interface, 1);
    display->SetPipShell(pipShell);
  } else if (iface.EqualsLiteral("xdg_wm_base")) {
    uint32_t vers = MIN(version, (uint32_t)xdg_wm_base_interface.version);
    auto* xdgWm = WaylandRegistryBind<xdg_wm_base>(
        registry, id, &xdg_wm_base_interface, vers);
    display->SetXdgWm(xdgWm);
  } else if (iface.EqualsLiteral("wl_output") &&
             version >= WL_OUTPUT_RELEASE_SINCE_VERSION) {
    auto* output = WaylandRegistryBind<wl_output>(
        registry, id, &wl_output_interface, WL_OUTPUT_RELEASE_SINCE_VERSION);
    display->AddMonitorConfig(id, output);
  } else if (iface.EqualsLiteral("wl_fixes")) {
    // wl_fixes_interface was introduced in libwayland-client 1.24, but
    // Ubuntu 22.04 still ships 1.20.
    static auto* sWlFixesInterface =
        (wl_interface*)dlsym(RTLD_DEFAULT, "wl_fixes_interface");
    if (sWlFixesInterface) {
      // Note that we cannot simply do MIN(version, 2) because the dynamically
      // loaded wl_fixes_interface can be v1, which can then lead to out of
      // bounds reads when sending v2 requests.
      const uint32_t libraryVersion = MIN(sWlFixesInterface->version, 2);
      auto* fixes = WaylandRegistryBind<wl_fixes>(
          registry, id, sWlFixesInterface, MIN(version, libraryVersion));
      display->SetFixes(fixes);
    } else {
      LOG("wl_fixes_interface is missing!");
    }
  }
}

static void global_registry_remover(void* data, wl_registry* registry,
                                    uint32_t id) {
  auto* display = static_cast<nsWaylandDisplay*>(data);
  if (!display) {
    return;
  }

  if (!display->RemoveMonitorConfig(id)) {
    display->RemoveSeat(id);
  }

  if (wl_fixes* fixes = display->GetFixes()) {
    if (wl_fixes_get_version(fixes) >=
        WL_FIXES_ACK_GLOBAL_REMOVE_SINCE_VERSION) {
      wl_fixes_ack_global_remove(fixes, registry, id);
    }
  }
}

static const struct wl_registry_listener registry_listener = {
    global_registry_handler, global_registry_remover};

nsWaylandDisplay::~nsWaylandDisplay() {
  g_list_free_full(mAsyncRoundtrips, (GDestroyNotify)wl_callback_destroy);
  MozClearPointer(mColorManager, wp_color_manager_v1_destroy);
  MozClearPointer(mColorRepresentationManager,
                  wp_color_representation_manager_v1_destroy);
  MozClearPointer(mWaylandSession, xdg_session_v1_destroy);
  MozClearPointer(mSessionManager, xdg_session_manager_v1_destroy);
}

void nsWaylandDisplay::AsyncRoundtripCallback(void* aData,
                                              wl_callback* aCallback,
                                              uint32_t aTime) {
  auto* display = static_cast<nsWaylandDisplay*>(aData);
  display->mAsyncRoundtrips =
      g_list_remove(display->mAsyncRoundtrips, aCallback);
  wl_callback_destroy(aCallback);
}

static const struct wl_callback_listener async_roundtrip_listener = {
    nsWaylandDisplay::AsyncRoundtripCallback};

void nsWaylandDisplay::RequestAsyncRoundtrip() {
  LOG("nsWaylandDisplay::RequestAsyncRoundtrip()");
  wl_callback* callback = wl_display_sync(mDisplay);
  wl_callback_add_listener(callback, &async_roundtrip_listener, this);
  mAsyncRoundtrips = g_list_append(mAsyncRoundtrips, callback);
  wl_display_flush(mDisplay);
}

void nsWaylandDisplay::WaitForAsyncRoundtrips() {
  LOG("nsWaylandDisplay::WaitForAsyncRoundtrips()");
  while (g_list_length(mAsyncRoundtrips) > 0) {
    if (wl_display_dispatch(mDisplay) < 0) {
      NS_WARNING("Failed to get events from Wayland display!");
      return;
    }
  }
}

void nsWaylandDisplay::RequestRoundtrip() {
  LOG("nsWaylandDisplay::RequestRoundtrip()");
  wl_display_roundtrip(mDisplay);
}

void nsWaylandDisplay::SessionCreate(void* aData, xdg_session_v1* aSession,
                                     const char* aSessionId) {
  LOG("nsWaylandDisplay::SessionCreate() %s", aSessionId);

  auto* display = static_cast<nsWaylandDisplay*>(aData);
  display->mWaylandSessionId = aSessionId;
  Preferences::SetCString("widget.wayland.session-id", aSessionId);
}

void nsWaylandDisplay::SessionRestore(void* aData, xdg_session_v1* aSession) {
#ifdef MOZ_LOGGING
  auto* display = static_cast<nsWaylandDisplay*>(aData);
  LOG("nsWaylandDisplay::SessionRestore() %s",
      display->mWaylandSessionId.get());
#endif
}

void nsWaylandDisplay::SessionReplace(void* aData, xdg_session_v1* aSession) {
  LOG("nsWaylandDisplay::SessionReplace()");
  auto* display = static_cast<nsWaylandDisplay*>(aData);
  display->CreateSession();
}

static const xdg_session_v1_listener sSessionListener = {
    nsWaylandDisplay::SessionCreate,
    nsWaylandDisplay::SessionRestore,
    nsWaylandDisplay::SessionReplace,
};

void nsWaylandDisplay::CreateSession(const char* aSessionId) {
  LOG("nsWaylandDisplay::CreateSession() ID %s", aSessionId);

  // TODO: WUniquePtr
  MozClearPointer(mWaylandSession, xdg_session_v1_destroy);

  mWaylandSession = xdg_session_manager_v1_get_session(
      mSessionManager,
      aSessionId ? XDG_SESSION_MANAGER_V1_REASON_SESSION_RESTORE
                 : XDG_SESSION_MANAGER_V1_REASON_LAUNCH,
      aSessionId);

  if (!mWaylandSession) {
    LOG("  failed to create session %s", aSessionId);
    return;
  }

  xdg_session_v1_add_listener(mWaylandSession, &sSessionListener, this);
}

void nsWaylandDisplay::SetSessionManager(
    xdg_session_manager_v1* aSessionManager) {
  LOG("nsWaylandDisplay::SetSessionManager()");
  mSessionManager = aSessionManager;
}

void nsWaylandDisplay::SessionManagerInit() {
  if (!mSessionManager ||
      !StaticPrefs::widget_wayland_session_management_enabled_AtStartup()) {
    return;
  }

  nsAutoCString prevSessionId;
  Preferences::GetCString("widget.wayland.session-id", prevSessionId);

  LOG("nsWaylandDisplay::SessionManagerInit() session ID '%s'",
      prevSessionId.get());
  CreateSession(prevSessionId.IsEmpty() ? nullptr : prevSessionId.get());
}

// Separate crash functions for different Wayland protocol error patterns.
// These functions are marked MOZ_NEVER_INLINE to ensure distinct crash
// signatures for different error types, making them easier to track and fix.
//
// Pattern analysis is based on 25 recent crash reports with the
// mozilla::widget::WlLogHandler signature. Frequencies are noted in comments
// below.

// 32% of crashes - Example: "unknown object (4278190083), message error(ous)"
// This is the one pattern where libwayland has already thrown the compositor's
// error text away, so add what wayland-proxy captured. That string is empty
// when nothing was captured. See bug 2039706.
MOZ_NEVER_INLINE static void WlLogHandler_UnknownObject(const char* error) {
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s Compositor error: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState(),
                          WaylandProxy::GetLastProtocolError());
}

// 20% of crashes - Example: "wp_viewport#296: error 2: source rectangle out
// of buffer bounds"
MOZ_NEVER_INLINE static void WlLogHandler_ViewportBufferBounds(
    const char* error) {
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState());
}

// 20% of crashes - Example: "wl_display#1: error 1: invalid arguments for
// wl_surface#730.frame"
MOZ_NEVER_INLINE static void WlLogHandler_InvalidSurfaceFrame(
    const char* error) {
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState());
}

// 12% of crashes - Example: "wp_color_manager_v1#44: error -1: Invalid output
// (2)"
MOZ_NEVER_INLINE static void WlLogHandler_ColorManagerInvalidOutput(
    const char* error) {
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState());
}

// 8% of crashes - Example: "Message length 22222 exceeds limit 4096"
MOZ_NEVER_INLINE static void WlLogHandler_MessageLengthExceeded(
    const char* error) {
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState());
}

// 4% of crashes - Example: "wl_display#1: error 0: invalid object 61246"
MOZ_NEVER_INLINE static void WlLogHandler_InvalidDisplayObject(
    const char* error) {
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState());
}

// dmabuf import failures. Mutter reports them as "failed to import supplied
// dmabufs: <reason>", and the reason names the step that failed, which also
// tracks the graphics driver. The two known reasons get their own functions;
// anything else goes to WlLogHandler_DmabufImportFailed. See bug 2035750.

// Example: "failed to import supplied dmabufs: Could not bind the given
// EGLImage to a CoglTexture2D". Seen with the proprietary NVIDIA driver.
MOZ_NEVER_INLINE static void WlLogHandler_DmabufImportFailed_EGLImageBind(
    const char* error) {
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState());
}

// Example: "failed to import supplied dmabufs: EGL failed to allocate
// resources for the requested operation." Seen with Mesa.
MOZ_NEVER_INLINE static void WlLogHandler_DmabufImportFailed_EGLAlloc(
    const char* error) {
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState());
}

// Any other dmabuf import failure that mentions EGL.
MOZ_NEVER_INLINE static void WlLogHandler_DmabufImportFailed(
    const char* error) {
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState());
}

// 4% of crashes - Example: "wl_registry@36: error 0: invalid global
// wl_output (55)"
MOZ_NEVER_INLINE static void WlLogHandler_RegistryError(const char* error) {
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState());
}

// 4% of crashes - Example: "invalid object (277), type (wl_callback), message
// begin(3uuou)"
MOZ_NEVER_INLINE static void WlLogHandler_CallbackInvalid(const char* error) {
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState());
}

// 4% of crashes - Example: "error marshalling arguments for set_surface
// (signature o): null value passed for arg 0"
MOZ_NEVER_INLINE static void WlLogHandler_MarshallingError(const char* error) {
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState());
}

// xdg_surface buffer mismatch - Example: "dg_wm_base@17: error 4: xdg_surface
// buffer (1 x 1) is larger than the configured fullscreen state (0 x 0)"
MOZ_NEVER_INLINE static void WlLogHandler_XdgSurfaceBufferMismatch(
    const char* error) {
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState());
}

// Compositor connection lost - Example: "Error reading events from display:
// Broken pipe". The Wayland compositor terminated our connection (e.g. session
// end, monitor hot-unplug); this is outside Firefox's control. See Bug 1984696.
MOZ_NEVER_INLINE static void WlLogHandler_DisplayReadError(const char* error) {
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState());
}

// Timestamp of the last "still attached" message ignored by WlLogHandler.
// Written on the main thread (libwayland event dispatch) with release ordering
// after writing sStillAttachedMessage, so any thread that observes the
// timestamp with acquire ordering is guaranteed to see the message too.
static std::atomic<clock_t> sStillAttachedTime{0};
static char sStillAttachedMessage[128];

static void WlLogHandler(const char* format, va_list args) {
  char error[1000];
  VsprintfLiteral(error, format, args);
  gfxCriticalNote << "(" << GetDesktopEnvironmentIdentifier().get()
                  << ") Wayland protocol error: " << error;

  // See Bug 1826583 and Bug 1844653 for reference.
  // "warning: queue %p destroyed while proxies still attached" and variants
  // like "zwp_linux_dmabuf_feedback_v1@%d still attached" are exceptions on
  // Wayland and non-fatal. They are triggered in certain versions of Mesa or
  // the proprietary Nvidia driver and we don't want to crash because of them.
  // Store the message and timestamp so ProcessFailure() can correlate this
  // event with a subsequent silent compositor disconnect.
  if (strstr(error, "still attached")) {
    // Sentinel: ensures a concurrent reader on the proxy thread never scans
    // past the end of the array if a longer new string overwrites the old
    // null terminator before placing its own.
    sStillAttachedMessage[sizeof(sStillAttachedMessage) - 1] = '\0';
    SprintfLiteral(sStillAttachedMessage, "%s", error);
    sStillAttachedTime.store(clock(), std::memory_order_release);
    return;
  }

  // Pattern matching to dispatch to specific crash functions.
  // Patterns ordered by frequency (most common first) for performance.

  // Pattern 1: Unknown object errors (32% of crashes)
  if (strstr(error, "unknown object") && strstr(error, "message error")) {
    WlLogHandler_UnknownObject(error);
  }

  // Pattern 2: wp_viewport buffer bounds errors (20% of crashes)
  if (strstr(error, "wp_viewport") &&
      strstr(error, "source rectangle out of buffer bounds")) {
    WlLogHandler_ViewportBufferBounds(error);
  }

  // Pattern 3: Invalid wl_surface.frame arguments (20% of crashes)
  if (strstr(error, "wl_display") && strstr(error, "invalid arguments") &&
      strstr(error, "wl_surface") && strstr(error, ".frame")) {
    WlLogHandler_InvalidSurfaceFrame(error);
  }

  // Pattern 4: wp_color_manager_v1 invalid output (12% of crashes)
  if (strstr(error, "wp_color_manager_v1") && strstr(error, "Invalid output")) {
    WlLogHandler_ColorManagerInvalidOutput(error);
  }

  // Pattern 5: Message length exceeded (8% of crashes)
  if ((strstr(error, "message length") || strstr(error, "Message length")) &&
      strstr(error, "exceeds")) {
    WlLogHandler_MessageLengthExceeded(error);
  }

  // Pattern 6: Invalid wl_display object (4% of crashes)
  if (strstr(error, "wl_display") && strstr(error, "invalid object") &&
      !strstr(error, "unknown object")) {
    WlLogHandler_InvalidDisplayObject(error);
  }

  // Pattern 7: dmabuf import failures (4% of crashes)
  if (strstr(error, "dmabuf") && strstr(error, "failed") &&
      strstr(error, "EGL")) {
    if (strstr(error, "Could not bind the given EGLImage")) {
      WlLogHandler_DmabufImportFailed_EGLImageBind(error);
    }
    if (strstr(error, "EGL failed to allocate resources")) {
      WlLogHandler_DmabufImportFailed_EGLAlloc(error);
    }
    WlLogHandler_DmabufImportFailed(error);
  }

  // Pattern 8: wl_registry errors (4% of crashes)
  if (strstr(error, "wl_registry") && strstr(error, "wl_output")) {
    WlLogHandler_RegistryError(error);
  }

  // Pattern 9: wl_callback invalid (4% of crashes)
  if (strstr(error, "wl_callback") && strstr(error, "invalid object")) {
    WlLogHandler_CallbackInvalid(error);
  }

  // Pattern 10: Marshalling errors (4% of crashes)
  if (strstr(error, "error marshalling arguments")) {
    WlLogHandler_MarshallingError(error);
  }

  // Pattern 11: xdg_surface buffer mismatch with fullscreen state
  if (strstr(error, "xdg_surface") && strstr(error, "buffer") &&
      strstr(error, "fullscreen state")) {
    WlLogHandler_XdgSurfaceBufferMismatch(error);
  }

  // Pattern 12: compositor connection lost (Bug 1984696)
  if (strstr(error, "Error reading events from display")) {
    WlLogHandler_DisplayReadError(error);
  }

  // Fallback for unmatched patterns - use original inline code
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState());
}

void WlCompositorUnavailableHandler() {
  gfxCriticalNote << "Wayland compositor unavailable ("
                  << GetDesktopEnvironmentIdentifier().get()
                  << "), proxy: " << WaylandProxy::GetState()
                  << " - scheduling graceful shutdown";
  // Called from the WaylandProxy thread. Dispatch to the main thread to
  // trigger a clean Firefox shutdown instead of crashing.
  NS_DispatchToMainThread(
      NS_NewRunnableFunction("WlCompositorUnavailableHandler", []() {
        nsCOMPtr<nsIAppStartup> appStartup =
            do_GetService("@mozilla.org/toolkit/app-startup;1");
        if (appStartup) {
          appStartup->Quit(nsIAppStartup::eForceQuit, 0);
        }
      }));
}

MOZ_NEVER_INLINE static void WlLogHandler_StillAttachedDisconnect(
    const char* error) {
  MOZ_CRASH_UNSAFE_PRINTF("(%s) %s Proxy: %s",
                          GetDesktopEnvironmentIdentifier().get(), error,
                          WaylandProxy::GetState());
}

void WlCompositorSilentDisconnectHandler(clock_t aFailureTime) {
  clock_t t = sStillAttachedTime.load(std::memory_order_acquire);
  if (t <= aFailureTime) {
    return;  // no still-attached event in this failure window
  }
  nsCString reason(sStillAttachedMessage);
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "WlCompositorSilentDisconnectHandler", [reason = std::move(reason)]() {
        WlLogHandler_StillAttachedDisconnect(reason.get());
      }));
}

nsWaylandDisplay::nsWaylandDisplay(wl_display* aDisplay)
    : mThreadId(PR_GetCurrentThread()), mDisplay(aDisplay) {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess());
  for (auto& e : mSupportedTransfer) {
    e = -1;
  };
  for (auto& e : mSupportedPrimaries) {
    e = -1;
  };
}

void nsWaylandDisplay::Init() {
  // GTK sets the log handler on display creation, thus we overwrite it here
  // in a similar fashion
  wl_log_set_handler_client(WlLogHandler);

  LOG("nsWaylandDisplay::Init()");

  mFormats = new DMABufFormats();
  mRegistry = wl_display_get_registry(mDisplay);
  wl_registry_add_listener(mRegistry, &registry_listener, this);
  wl_display_roundtrip(mDisplay);
  RequestAsyncRoundtrip();
  WaitForAsyncRoundtrips();
  EnsureDMABufFormats();

  LOG("  init finished");

  // Check we have critical Wayland interfaces.
  // Missing ones indicates a compositor bug/missing feature and
  // we can't continue.
  MOZ_RELEASE_ASSERT(GetShm(), "We're missing shm interface!");
  MOZ_RELEASE_ASSERT(GetCompositor(), "We're missing compositor interface!");
  MOZ_RELEASE_ASSERT(GetSubcompositor(),
                     "We're missing subcompositor interface!");
  if (!GetViewporter()) {
    NS_WARNING("Missing viewporter wayland protocol!");
  }
  if (!GetFractionalScaleManager()) {
    NS_WARNING("Missing wp_fractional_scale_v1 wayland protocol!");
  }
}

bool nsWaylandDisplay::IsTFSupported(uint32_t aTF) {
  if (aTF < sColorTransfersNum) {
    return mSupportedTransfer[aTF] == (int)aTF;
  } else {
    NS_WARNING("Unknow color transfer function!");
    return false;
  }
}

bool nsWaylandDisplay::IsSetMDCVSupported() {
  return mColorManagerSupportedFeature.mDisplayPrimaries;
}

bool nsWaylandDisplay::IsSetLuminancesSupported() {
  return mColorManagerSupportedFeature.mLuminances;
}

bool nsWaylandDisplay::IsPrimariesSupported(uint32_t aPrimaries) {
  if (aPrimaries < sColorPrimariesNum) {
    return mSupportedPrimaries[aPrimaries] == (int)aPrimaries;
  } else {
    NS_WARNING("Unknow color primaries!");
    return false;
  }
}

bool nsWaylandDisplay::IsParametricSupported() {
  return mColorManagerSupportedFeature.mParametric;
}

}  // namespace mozilla::widget
