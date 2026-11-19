/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <cstddef>

#ifndef UNICODE
#  define UNICODE
#endif
// clang-format off
#include <windows.h>
#include <hidsdi.h>
#include <stdio.h>
#include <xinput.h>
// clang-format on

#include "Gamepad.h"
#include "mozilla/dom/GamepadPlatformService.h"
#include "mozilla/dom/GamepadRemapping.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "nsITimer.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "nsWindowsHelpers.h"

namespace {

using namespace mozilla;
using namespace mozilla::dom;

// USB HID usage tables, page 1, 0x30 = X
const uint32_t kAxisMinimumUsageNumber = 0x30;
// USB HID usage tables, page 1 (Hat switch)
const uint32_t kAxesLengthCap = 16;

// USB HID usage tables
const uint32_t kDesktopUsagePage = 0x1;
const uint32_t kButtonUsagePage = 0x9;

// Multiple devices-changed notifications can be sent when a device
// is connected, because USB devices consist of multiple logical devices.
// Therefore, we wait a bit after receiving one before looking for
// device changes.
const uint32_t kDevicesChangedStableDelay = 200;
// Both DirectInput and XInput are polling-driven here,
// so we need to poll it periodically.
// 4ms, or 250 Hz, is consistent with Chrome's gamepad implementation.
const uint32_t kWindowsGamepadPollInterval = 4;

const UINT kRawInputError = (UINT)-1;

// In XInputGetState, we can't get the state of Xbox Guide button.
// We need to go through the undocumented XInputGetStateEx method
// to get that button's state.
const LPCSTR kXInputGetStateExOrdinal = (LPCSTR)100;
// Bitmask for the Guide button in XInputGamepadEx.wButtons.
const int XINPUT_GAMEPAD_Guide = 0x0400;

#ifndef XUSER_MAX_COUNT
#  define XUSER_MAX_COUNT 4
#endif

const struct {
  int usagePage;
  int usage;
} kUsagePages[] = {
    // USB HID usage tables, page 1
    {kDesktopUsagePage, 4},  // Joystick
    {kDesktopUsagePage, 5}   // Gamepad
};

const struct {
  WORD button;
  int mapped;
} kXIButtonMap[] = {{XINPUT_GAMEPAD_DPAD_UP, 12},
                    {XINPUT_GAMEPAD_DPAD_DOWN, 13},
                    {XINPUT_GAMEPAD_DPAD_LEFT, 14},
                    {XINPUT_GAMEPAD_DPAD_RIGHT, 15},
                    {XINPUT_GAMEPAD_START, 9},
                    {XINPUT_GAMEPAD_BACK, 8},
                    {XINPUT_GAMEPAD_LEFT_THUMB, 10},
                    {XINPUT_GAMEPAD_RIGHT_THUMB, 11},
                    {XINPUT_GAMEPAD_LEFT_SHOULDER, 4},
                    {XINPUT_GAMEPAD_RIGHT_SHOULDER, 5},
                    {XINPUT_GAMEPAD_Guide, 16},
                    {XINPUT_GAMEPAD_A, 0},
                    {XINPUT_GAMEPAD_B, 1},
                    {XINPUT_GAMEPAD_X, 2},
                    {XINPUT_GAMEPAD_Y, 3}};
const size_t kNumMappings = std::size(kXIButtonMap);

enum GamepadType { kNoGamepad = 0, kRawInputGamepad, kXInputGamepad };

class WindowsGamepadService;
// This pointer holds a windows gamepad backend service,
// it will be created and destroyed by background thread and
// used by gMonitorThread
WindowsGamepadService* MOZ_NON_OWNING_REF gService = nullptr;
constinit nsCOMPtr<nsIThread> gMonitorThread;
static bool sIsShutdown = false;

class Gamepad {
 public:
  GamepadType type;

  // Handle to raw input device
  HANDLE handle;

  // XInput Index of the user's controller. Passed to XInputGetState.
  DWORD userIndex;

  // Last-known state of the controller.
  XINPUT_STATE state;

  // Handle from the GamepadService
  GamepadHandle gamepadHandle;

  // Information about the physical device.
  unsigned numAxes;
  unsigned numButtons;

  // USB vendor/product id (raw-input devices), used to recognize controllers
  // that need custom report handling.
  uint16_t vendorId = 0;
  uint16_t productId = 0;

  nsTArray<bool> buttons;
  struct axisValue {
    HIDP_VALUE_CAPS caps;
    double value;
    bool active;

    axisValue() : value(0.0f), active(false) {}
    explicit axisValue(const HIDP_VALUE_CAPS& aCaps)
        : caps(aCaps), value(0.0f), active(true) {}
  };
  nsTArray<axisValue> axes;

  RefPtr<GamepadRemapper> remapper;

  // Used during rescan to find devices that were disconnected.
  bool present;

  Gamepad(uint32_t aNumAxes, uint32_t aNumButtons, GamepadType aType)
      : type(aType), numAxes(aNumAxes), numButtons(aNumButtons), present(true) {
    buttons.SetLength(numButtons);
    axes.SetLength(numAxes);
  }

 private:
  Gamepad() = default;
};

// Drop this in favor of decltype when we require a new enough SDK.
using XInputEnable_func = void(WINAPI*)(BOOL);

// RAII class to wrap loading the XInput DLL
class XInputLoader {
 public:
  XInputLoader()
      : module(nullptr), mXInputGetState(nullptr), mXInputEnable(nullptr) {
    // xinput1_4.dll exists on Windows 8
    // xinput9_1_0.dll exists on Windows 7 and Vista
    // xinput1_3.dll shipped with the DirectX SDK
    const wchar_t* dlls[] = {L"xinput1_4.dll", L"xinput9_1_0.dll",
                             L"xinput1_3.dll"};
    const size_t kNumDLLs = std::size(dlls);
    for (size_t i = 0; i < kNumDLLs; ++i) {
      module = LoadLibraryW(dlls[i]);
      if (module) {
        mXInputEnable = reinterpret_cast<XInputEnable_func>(
            GetProcAddress(module, "XInputEnable"));
        // Checking if `XInputGetStateEx` is available. If not,
        // we will fallback to use `XInputGetState`.
        mXInputGetState = reinterpret_cast<decltype(XInputGetState)*>(
            GetProcAddress(module, kXInputGetStateExOrdinal));
        if (!mXInputGetState) {
          mXInputGetState = reinterpret_cast<decltype(XInputGetState)*>(
              GetProcAddress(module, "XInputGetState"));
        }
        MOZ_ASSERT(mXInputGetState &&
                   "XInputGetState must be linked successfully.");

        if (mXInputEnable) {
          mXInputEnable(TRUE);
        }
        break;
      }
    }
  }

  ~XInputLoader() {
    mXInputEnable = nullptr;
    mXInputGetState = nullptr;

    if (module) {
      FreeLibrary(module);
    }
  }

  explicit operator bool() { return module && mXInputGetState; }

  HMODULE module;
  decltype(XInputGetState)* mXInputGetState;
  XInputEnable_func mXInputEnable;
};

bool GetPreparsedData(HANDLE handle, nsTArray<uint8_t>& data) {
  UINT size;
  if (GetRawInputDeviceInfo(handle, RIDI_PREPARSEDDATA, nullptr, &size) ==
      kRawInputError) {
    return false;
  }
  data.SetLength(size);
  return GetRawInputDeviceInfo(handle, RIDI_PREPARSEDDATA, data.Elements(),
                               &size) > 0;
}

/*
 * Given an axis value and a minimum and maximum range,
 * scale it to be in the range -1.0 .. 1.0.
 */
double ScaleAxis(ULONG value, LONG min, LONG max) {
  return 2.0 * (value - min) / (max - min) - 1.0;
}

// Nintendo Switch Pro controller (also reported by 8BitDo pads in Switch mode).
// Its HID report descriptor doesn't describe the 0x30 full input report it
// streams over USB, so HidP_* extracts garbage; the report is parsed by hand
// instead (see WindowsGamepadService::ParseSwitchProReport).
const uint16_t kSwitchProVendorId = 0x057e;
const uint16_t kSwitchProProductId = 0x2009;
const uint8_t kSwitchProFullReportId = 0x30;
// Default analog-stick calibration; per-device SPI calibration is not read.
const int kSwitchStickCenter = 2050;
const int kSwitchStickMin = 550;
const int kSwitchStickMax = 3550;
const int kSwitchStickDeadzone = 160;

// Normalize a 12-bit Switch Pro stick axis to -1.0 .. 1.0 with default
// calibration.
double NormalizeSwitchAxis(uint16_t value) {
  const double scaled = 2.0 * (static_cast<double>(value) - kSwitchStickMin) /
                            (kSwitchStickMax - kSwitchStickMin) -
                        1.0;
  return std::clamp(scaled, -1.0, 1.0);
}

/*
 * Return true if this USB HID usage page and usage are of a type we
 * know how to handle.
 */
bool SupportedUsage(USHORT page, USHORT usage) {
  for (unsigned i = 0; i < std::size(kUsagePages); i++) {
    if (page == kUsagePages[i].usagePage && usage == kUsagePages[i].usage) {
      return true;
    }
  }
  return false;
}

class HIDLoader {
 public:
  HIDLoader()
      : mHidD_GetProductString(nullptr),
        mHidP_GetCaps(nullptr),
        mHidP_GetButtonCaps(nullptr),
        mHidP_GetValueCaps(nullptr),
        mHidP_GetUsages(nullptr),
        mHidP_GetUsageValue(nullptr),
        mHidP_GetScaledUsageValue(nullptr),
        mModule(LoadLibraryW(L"hid.dll")) {
    if (mModule) {
      mHidD_GetProductString =
          reinterpret_cast<decltype(HidD_GetProductString)*>(
              GetProcAddress(mModule, "HidD_GetProductString"));
      mHidP_GetCaps = reinterpret_cast<decltype(HidP_GetCaps)*>(
          GetProcAddress(mModule, "HidP_GetCaps"));
      mHidP_GetButtonCaps = reinterpret_cast<decltype(HidP_GetButtonCaps)*>(
          GetProcAddress(mModule, "HidP_GetButtonCaps"));
      mHidP_GetValueCaps = reinterpret_cast<decltype(HidP_GetValueCaps)*>(
          GetProcAddress(mModule, "HidP_GetValueCaps"));
      mHidP_GetUsages = reinterpret_cast<decltype(HidP_GetUsages)*>(
          GetProcAddress(mModule, "HidP_GetUsages"));
      mHidP_GetUsageValue = reinterpret_cast<decltype(HidP_GetUsageValue)*>(
          GetProcAddress(mModule, "HidP_GetUsageValue"));
      mHidP_GetScaledUsageValue =
          reinterpret_cast<decltype(HidP_GetScaledUsageValue)*>(
              GetProcAddress(mModule, "HidP_GetScaledUsageValue"));
    }
  }

  ~HIDLoader() {
    if (mModule) {
      FreeLibrary(mModule);
    }
  }

  explicit operator bool() {
    return mModule && mHidD_GetProductString && mHidP_GetCaps &&
           mHidP_GetButtonCaps && mHidP_GetValueCaps && mHidP_GetUsages &&
           mHidP_GetUsageValue && mHidP_GetScaledUsageValue;
  }

  decltype(HidD_GetProductString)* mHidD_GetProductString;
  decltype(HidP_GetCaps)* mHidP_GetCaps;
  decltype(HidP_GetButtonCaps)* mHidP_GetButtonCaps;
  decltype(HidP_GetValueCaps)* mHidP_GetValueCaps;
  decltype(HidP_GetUsages)* mHidP_GetUsages;
  decltype(HidP_GetUsageValue)* mHidP_GetUsageValue;
  decltype(HidP_GetScaledUsageValue)* mHidP_GetScaledUsageValue;

 private:
  HMODULE mModule;
};

HWND sHWnd = nullptr;

static void DirectInputMessageLoopOnceCallback(nsITimer* aTimer,
                                               void* aClosure) {
  MOZ_ASSERT(NS_GetCurrentThread() == gMonitorThread);
  MSG msg;
  while (PeekMessageW(&msg, sHWnd, 0, 0, PM_REMOVE) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  aTimer->Cancel();
  if (!sIsShutdown) {
    aTimer->InitWithNamedFuncCallback(DirectInputMessageLoopOnceCallback,
                                      nullptr, kWindowsGamepadPollInterval,
                                      nsITimer::TYPE_ONE_SHOT,
                                      "DirectInputMessageLoopOnceCallback"_ns);
  }
}

class WindowsGamepadService {
 public:
  WindowsGamepadService() {
    mDirectInputTimer = NS_NewTimer();
    mXInputTimer = NS_NewTimer();
    mDeviceChangeTimer = NS_NewTimer();
  }
  virtual ~WindowsGamepadService() { Cleanup(); }

  void DevicesChanged(bool aIsStablizing);

  void StartMessageLoop() {
    MOZ_ASSERT(mDirectInputTimer);
    mDirectInputTimer->InitWithNamedFuncCallback(
        DirectInputMessageLoopOnceCallback, nullptr,
        kWindowsGamepadPollInterval, nsITimer::TYPE_ONE_SHOT,
        "DirectInputMessageLoopOnceCallback"_ns);
  }

  void Startup();
  void Shutdown();
  // Parse gamepad input from a WM_INPUT message.
  bool HandleRawInput(HRAWINPUT handle);
  // Parse a Nintendo Switch Pro 0x30 full input report directly, emitting
  // button/axis events. Used instead of the HID-usage path, which the device's
  // report descriptor doesn't support.
  void ParseSwitchProReport(Gamepad& aGamepad, const BYTE* aData);
  void SetLightIndicatorColor(const Tainted<GamepadHandle>& aGamepadHandle,
                              const Tainted<uint32_t>& aLightColorIndex,
                              const uint8_t& aRed, const uint8_t& aGreen,
                              const uint8_t& aBlue);
  size_t WriteOutputReport(const std::vector<uint8_t>& aReport);
  static void XInputMessageLoopOnceCallback(nsITimer* aTimer, void* aClosure);
  static void DevicesChangeCallback(nsITimer* aTimer, void* aService);

 private:
  void ScanForDevices();
  // Look for connected raw input devices.
  void ScanForRawInputDevices();
  // Look for connected XInput devices.
  bool ScanForXInputDevices();
  bool HaveXInputGamepad(unsigned int userIndex);

  bool mIsXInputMonitoring;
  void PollXInput();
  void CheckXInputChanges(Gamepad& gamepad, XINPUT_STATE& state);

  // Get information about a raw input gamepad.
  bool GetRawGamepad(HANDLE handle);
  void Cleanup();

  // List of connected devices.
  nsTArray<Gamepad> mGamepads;

  HIDLoader mHID;
  nsAutoHandle mHidHandle;
  XInputLoader mXInput;

  nsCOMPtr<nsITimer> mDirectInputTimer;
  nsCOMPtr<nsITimer> mXInputTimer;
  nsCOMPtr<nsITimer> mDeviceChangeTimer;
};

void WindowsGamepadService::ScanForRawInputDevices() {
  if (!mHID) {
    return;
  }

  UINT numDevices;
  if (GetRawInputDeviceList(nullptr, &numDevices, sizeof(RAWINPUTDEVICELIST)) ==
      kRawInputError) {
    return;
  }
  nsTArray<RAWINPUTDEVICELIST> devices(numDevices);
  devices.SetLength(numDevices);
  if (GetRawInputDeviceList(devices.Elements(), &numDevices,
                            sizeof(RAWINPUTDEVICELIST)) == kRawInputError) {
    return;
  }

  for (unsigned i = 0; i < devices.Length(); i++) {
    if (devices[i].dwType == RIM_TYPEHID) {
      GetRawGamepad(devices[i].hDevice);
    }
  }
}

// static
void WindowsGamepadService::XInputMessageLoopOnceCallback(nsITimer* aTimer,
                                                          void* aService) {
  MOZ_ASSERT(aService);
  WindowsGamepadService* self = static_cast<WindowsGamepadService*>(aService);
  self->PollXInput();
  if (self->mIsXInputMonitoring) {
    aTimer->Cancel();
    aTimer->InitWithNamedFuncCallback(
        XInputMessageLoopOnceCallback, self, kWindowsGamepadPollInterval,
        nsITimer::TYPE_ONE_SHOT, "XInputMessageLoopOnceCallback"_ns);
  }
}

// static
void WindowsGamepadService::DevicesChangeCallback(nsITimer* aTimer,
                                                  void* aService) {
  MOZ_ASSERT(aService);
  WindowsGamepadService* self = static_cast<WindowsGamepadService*>(aService);
  self->DevicesChanged(false);
}

bool WindowsGamepadService::HaveXInputGamepad(unsigned int userIndex) {
  for (unsigned int i = 0; i < mGamepads.Length(); i++) {
    if (mGamepads[i].type == kXInputGamepad &&
        mGamepads[i].userIndex == userIndex) {
      mGamepads[i].present = true;
      return true;
    }
  }
  return false;
}

bool WindowsGamepadService::ScanForXInputDevices() {
  MOZ_ASSERT(mXInput, "XInput should be present!");

  bool found = false;
  RefPtr<GamepadPlatformService> service =
      GamepadPlatformService::GetParentService();
  if (!service) {
    return found;
  }

  for (unsigned int i = 0; i < XUSER_MAX_COUNT; i++) {
    XINPUT_STATE state = {};

    if (!mXInput.mXInputGetState ||
        mXInput.mXInputGetState(i, &state) != ERROR_SUCCESS) {
      continue;
    }

    found = true;
    // See if this device is already present in our list.
    if (HaveXInputGamepad(i)) {
      continue;
    }

    // Not already present, add it.
    Gamepad gamepad(kStandardGamepadAxes, kStandardGamepadButtons,
                    kXInputGamepad);
    gamepad.userIndex = i;
    gamepad.state = state;
    gamepad.gamepadHandle = service->AddGamepad(
        "xinput", GamepadMappingType::Standard, GamepadHand::_empty,
        kStandardGamepadButtons, kStandardGamepadAxes, 0, 0,
        0);  // TODO: Bug 680289, implement gamepad haptics for Windows.
    mGamepads.AppendElement(std::move(gamepad));
  }

  return found;
}

void WindowsGamepadService::ScanForDevices() {
  RefPtr<GamepadPlatformService> service =
      GamepadPlatformService::GetParentService();
  if (!service) {
    return;
  }

  for (int i = mGamepads.Length() - 1; i >= 0; i--) {
    mGamepads[i].present = false;
  }

  if (mHID) {
    ScanForRawInputDevices();
  }
  if (mXInput) {
    mXInputTimer->Cancel();
    if (ScanForXInputDevices()) {
      mIsXInputMonitoring = true;
      mXInputTimer->InitWithNamedFuncCallback(
          XInputMessageLoopOnceCallback, this, kWindowsGamepadPollInterval,
          nsITimer::TYPE_ONE_SHOT, "XInputMessageLoopOnceCallback"_ns);
    } else {
      mIsXInputMonitoring = false;
    }
  }

  // Look for devices that are no longer present and remove them.
  for (int i = mGamepads.Length() - 1; i >= 0; i--) {
    if (!mGamepads[i].present) {
      service->RemoveGamepad(mGamepads[i].gamepadHandle);
      mGamepads.RemoveElementAt(i);
    }
  }
}

void WindowsGamepadService::PollXInput() {
  for (unsigned int i = 0; i < mGamepads.Length(); i++) {
    if (mGamepads[i].type != kXInputGamepad) {
      continue;
    }

    XINPUT_STATE state = {};

    if (!mXInput.mXInputGetState ||
        mXInput.mXInputGetState(mGamepads[i].userIndex, &state) !=
            ERROR_SUCCESS) {
      continue;
    }

    if (state.dwPacketNumber != mGamepads[i].state.dwPacketNumber) {
      CheckXInputChanges(mGamepads[i], state);
    }
  }
}

void WindowsGamepadService::CheckXInputChanges(Gamepad& gamepad,
                                               XINPUT_STATE& state) {
  RefPtr<GamepadPlatformService> service =
      GamepadPlatformService::GetParentService();
  if (!service) {
    return;
  }
  // Handle digital buttons first
  for (size_t b = 0; b < kNumMappings; b++) {
    if (state.Gamepad.wButtons & kXIButtonMap[b].button &&
        !(gamepad.state.Gamepad.wButtons & kXIButtonMap[b].button)) {
      // Button pressed
      service->NewButtonEvent(gamepad.gamepadHandle, kXIButtonMap[b].mapped,
                              true);
    } else if (!(state.Gamepad.wButtons & kXIButtonMap[b].button) &&
               gamepad.state.Gamepad.wButtons & kXIButtonMap[b].button) {
      // Button released
      service->NewButtonEvent(gamepad.gamepadHandle, kXIButtonMap[b].mapped,
                              false);
    }
  }

  // Then triggers
  if (state.Gamepad.bLeftTrigger != gamepad.state.Gamepad.bLeftTrigger) {
    const bool pressed =
        state.Gamepad.bLeftTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    service->NewButtonEvent(gamepad.gamepadHandle, kButtonLeftTrigger, pressed,
                            state.Gamepad.bLeftTrigger / 255.0);
  }
  if (state.Gamepad.bRightTrigger != gamepad.state.Gamepad.bRightTrigger) {
    const bool pressed =
        state.Gamepad.bRightTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    service->NewButtonEvent(gamepad.gamepadHandle, kButtonRightTrigger, pressed,
                            state.Gamepad.bRightTrigger / 255.0);
  }

  // Finally deal with analog sticks
  // TODO: bug 1001955 - Support deadzones.
  if (state.Gamepad.sThumbLX != gamepad.state.Gamepad.sThumbLX) {
    const float div = state.Gamepad.sThumbLX > 0 ? 32767.0 : 32768.0;
    service->NewAxisMoveEvent(gamepad.gamepadHandle, kLeftStickXAxis,
                              state.Gamepad.sThumbLX / div);
  }
  if (state.Gamepad.sThumbLY != gamepad.state.Gamepad.sThumbLY) {
    const float div = state.Gamepad.sThumbLY > 0 ? 32767.0 : 32768.0;
    service->NewAxisMoveEvent(gamepad.gamepadHandle, kLeftStickYAxis,
                              -1.0 * state.Gamepad.sThumbLY / div);
  }
  if (state.Gamepad.sThumbRX != gamepad.state.Gamepad.sThumbRX) {
    const float div = state.Gamepad.sThumbRX > 0 ? 32767.0 : 32768.0;
    service->NewAxisMoveEvent(gamepad.gamepadHandle, kRightStickXAxis,
                              state.Gamepad.sThumbRX / div);
  }
  if (state.Gamepad.sThumbRY != gamepad.state.Gamepad.sThumbRY) {
    const float div = state.Gamepad.sThumbRY > 0 ? 32767.0 : 32768.0;
    service->NewAxisMoveEvent(gamepad.gamepadHandle, kRightStickYAxis,
                              -1.0 * state.Gamepad.sThumbRY / div);
  }
  gamepad.state = state;
}

// Used to sort a list of axes by HID usage.
class HidValueComparator {
 public:
  bool Equals(const Gamepad::axisValue& c1,
              const Gamepad::axisValue& c2) const {
    return c1.caps.UsagePage == c2.caps.UsagePage &&
           c1.caps.Range.UsageMin == c2.caps.Range.UsageMin;
  }
  bool LessThan(const Gamepad::axisValue& c1,
                const Gamepad::axisValue& c2) const {
    if (c1.caps.UsagePage == c2.caps.UsagePage) {
      return c1.caps.Range.UsageMin < c2.caps.Range.UsageMin;
    }
    return c1.caps.UsagePage < c2.caps.UsagePage;
  }
};

// GetRawGamepad() processes its raw data from HID and
// then trying to remapping buttons and axes based on
// the mapping rules that are defined for different gamepad products.
bool WindowsGamepadService::GetRawGamepad(HANDLE handle) {
  RefPtr<GamepadPlatformService> service =
      GamepadPlatformService::GetParentService();
  if (!service) {
    return true;
  }

  if (!mHID) {
    return false;
  }

  for (unsigned i = 0; i < mGamepads.Length(); i++) {
    if (mGamepads[i].type == kRawInputGamepad &&
        mGamepads[i].handle == handle) {
      mGamepads[i].present = true;
      return true;
    }
  }

  RID_DEVICE_INFO rdi = {};
  UINT size = rdi.cbSize = sizeof(RID_DEVICE_INFO);
  if (GetRawInputDeviceInfo(handle, RIDI_DEVICEINFO, &rdi, &size) ==
      kRawInputError) {
    return false;
  }
  // Ensure that this is a device we care about
  if (!SupportedUsage(rdi.hid.usUsagePage, rdi.hid.usUsage)) {
    return false;
  }

  // Device name is a mostly-opaque string.
  if (GetRawInputDeviceInfo(handle, RIDI_DEVICENAME, nullptr, &size) ==
      kRawInputError) {
    return false;
  }

  nsTArray<wchar_t> devname(size);
  devname.SetLength(size);
  if (GetRawInputDeviceInfo(handle, RIDI_DEVICENAME, devname.Elements(),
                            &size) == kRawInputError) {
    return false;
  }

  // Per http://msdn.microsoft.com/en-us/library/windows/desktop/ee417014.aspx
  // device names containing "IG_" are XInput controllers. Ignore those
  // devices since we'll handle them with XInput.
  if (wcsstr(devname.Elements(), L"IG_")) {
    return false;
  }

  // Product string is a human-readable name.
  // Per
  // http://msdn.microsoft.com/en-us/library/windows/hardware/ff539681%28v=vs.85%29.aspx
  // "For USB devices, the maximum string length is 126 wide characters (not
  // including the terminating NULL character)."
  wchar_t name[128] = {0};
  size = sizeof(name);
  nsTArray<char> gamepad_name;
  // Creating this file with FILE_FLAG_OVERLAPPED to perform
  // an asynchronous request in WriteOutputReport.
  mHidHandle.own(CreateFile(devname.Elements(), GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
  if (mHidHandle != INVALID_HANDLE_VALUE) {
    if (mHID.mHidD_GetProductString(mHidHandle, &name, size)) {
      int bytes = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr,
                                      nullptr);
      gamepad_name.SetLength(bytes);
      WideCharToMultiByte(CP_UTF8, 0, name, -1, gamepad_name.Elements(), bytes,
                          nullptr, nullptr);
    }
  }
  if (gamepad_name.Length() == 0 || !gamepad_name[0]) {
    const char kUnknown[] = "Unknown Gamepad";
    gamepad_name.SetLength(std::size(kUnknown));
    strcpy_s(gamepad_name.Elements(), gamepad_name.Length(), kUnknown);
  }

  char gamepad_id[256] = {0};
  _snprintf_s(gamepad_id, _TRUNCATE, "%04x-%04x-%s", rdi.hid.dwVendorId,
              rdi.hid.dwProductId, gamepad_name.Elements());

  nsTArray<uint8_t> preparsedbytes;
  if (!GetPreparsedData(handle, preparsedbytes)) {
    return false;
  }

  PHIDP_PREPARSED_DATA parsed =
      reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsedbytes.Elements());
  HIDP_CAPS caps;
  if (mHID.mHidP_GetCaps(parsed, &caps) != HIDP_STATUS_SUCCESS) {
    return false;
  }

  // Enumerate buttons.
  USHORT count = caps.NumberInputButtonCaps;
  nsTArray<HIDP_BUTTON_CAPS> buttonCaps(count);
  buttonCaps.SetLength(count);
  if (mHID.mHidP_GetButtonCaps(HidP_Input, buttonCaps.Elements(), &count,
                               parsed) != HIDP_STATUS_SUCCESS) {
    return false;
  }
  uint32_t numButtons = 0;
  for (unsigned i = 0; i < count; i++) {
    // Each buttonCaps is typically a range of buttons.
    numButtons +=
        buttonCaps[i].Range.UsageMax - buttonCaps[i].Range.UsageMin + 1;
  }

  // Enumerate value caps, which represent axes and d-pads.
  count = caps.NumberInputValueCaps;
  nsTArray<HIDP_VALUE_CAPS> axisCaps(count);
  axisCaps.SetLength(count);
  if (mHID.mHidP_GetValueCaps(HidP_Input, axisCaps.Elements(), &count,
                              parsed) != HIDP_STATUS_SUCCESS) {
    return false;
  }

  size_t numAxes = 0;
  nsTArray<Gamepad::axisValue> axes(kAxesLengthCap);
  // We store these value caps and handle the dpad info in GamepadRemapper
  // later.
  axes.SetLength(kAxesLengthCap);

  // Looking for the exisiting ramapping rule.
  bool defaultRemapper = false;
  RefPtr<GamepadRemapper> remapper = GetGamepadRemapper(
      rdi.hid.dwVendorId, rdi.hid.dwProductId, defaultRemapper);
  MOZ_ASSERT(remapper);

  for (size_t i = 0; i < count; i++) {
    const size_t axisIndex =
        axisCaps[i].Range.UsageMin - kAxisMinimumUsageNumber;
    if (axisIndex < kAxesLengthCap && !axes[axisIndex].active) {
      axes[axisIndex].caps = axisCaps[i];
      axes[axisIndex].active = true;
      numAxes = std::max(numAxes, axisIndex + 1);
    }
  }

  // Not already present, add it.

  remapper->SetAxisCount(numAxes);
  remapper->SetButtonCount(numButtons);
  Gamepad gamepad(numAxes, numButtons, kRawInputGamepad);
  gamepad.handle = handle;
  gamepad.vendorId = static_cast<uint16_t>(rdi.hid.dwVendorId);
  gamepad.productId = static_cast<uint16_t>(rdi.hid.dwProductId);

  for (unsigned i = 0; i < gamepad.numAxes; i++) {
    gamepad.axes[i] = axes[i];
  }

  gamepad.remapper = remapper.forget();
  // TODO: Bug 680289, implement gamepad haptics for Windows.
  gamepad.gamepadHandle = service->AddGamepad(
      gamepad_id, gamepad.remapper->GetMappingType(), GamepadHand::_empty,
      gamepad.remapper->GetButtonCount(), gamepad.remapper->GetAxisCount(), 0,
      gamepad.remapper->GetLightIndicatorCount(),
      gamepad.remapper->GetTouchEventCount());

  nsTArray<GamepadLightIndicatorType> lightTypes;
  gamepad.remapper->GetLightIndicators(lightTypes);
  for (uint32_t i = 0; i < lightTypes.Length(); ++i) {
    if (lightTypes[i] != GamepadLightIndicator::DefaultType()) {
      service->NewLightIndicatorTypeEvent(gamepad.gamepadHandle, i,
                                          lightTypes[i]);
    }
  }

  mGamepads.AppendElement(std::move(gamepad));
  return true;
}

void WindowsGamepadService::ParseSwitchProReport(Gamepad& aGamepad,
                                                 const BYTE* aData) {
  RefPtr<GamepadPlatformService> service =
      GamepadPlatformService::GetParentService();
  if (!service) {
    return;
  }

  // 0x30 full report layout: [3]=right-side buttons, [4]=shared buttons,
  // [5]=left-side buttons, [6..8]=left stick, [9..11]=right stick.
  const uint8_t rb = aData[3];
  const uint8_t sb = aData[4];
  const uint8_t lb = aData[5];

  auto setButton = [&](uint32_t aIndex, bool aPressed) {
    if (aIndex < aGamepad.buttons.Length() &&
        aGamepad.buttons[aIndex] != aPressed) {
      service->NewButtonEvent(aGamepad.gamepadHandle, aIndex, aPressed);
      aGamepad.buttons[aIndex] = aPressed;
    }
  };

  // Nintendo face buttons map to the Standard Gamepad by physical position.
  setButton(BUTTON_INDEX_PRIMARY, rb & 0x04);         // B (bottom)
  setButton(BUTTON_INDEX_SECONDARY, rb & 0x08);       // A (right)
  setButton(BUTTON_INDEX_TERTIARY, rb & 0x01);        // Y (left)
  setButton(BUTTON_INDEX_QUATERNARY, rb & 0x02);      // X (top)
  setButton(BUTTON_INDEX_LEFT_SHOULDER, lb & 0x40);   // L
  setButton(BUTTON_INDEX_RIGHT_SHOULDER, rb & 0x40);  // R
  setButton(BUTTON_INDEX_LEFT_TRIGGER, lb & 0x80);    // ZL
  setButton(BUTTON_INDEX_RIGHT_TRIGGER, rb & 0x80);   // ZR
  setButton(BUTTON_INDEX_BACK_SELECT, sb & 0x01);     // Minus
  setButton(BUTTON_INDEX_START, sb & 0x02);           // Plus
  setButton(BUTTON_INDEX_LEFT_THUMBSTICK, sb & 0x08);
  setButton(BUTTON_INDEX_RIGHT_THUMBSTICK, sb & 0x04);
  setButton(BUTTON_INDEX_DPAD_UP, lb & 0x02);
  setButton(BUTTON_INDEX_DPAD_DOWN, lb & 0x01);
  setButton(BUTTON_INDEX_DPAD_LEFT, lb & 0x08);
  setButton(BUTTON_INDEX_DPAD_RIGHT, lb & 0x04);
  setButton(BUTTON_INDEX_META, sb & 0x10);      // Home
  setButton(BUTTON_INDEX_META + 1, sb & 0x20);  // Capture

  // Each stick is two 12-bit values packed across three bytes.
  const uint16_t lx = aData[6] | ((aData[7] & 0x0f) << 8);
  const uint16_t ly = (aData[8] << 4) | (aData[7] >> 4);
  const uint16_t rx = aData[9] | ((aData[10] & 0x0f) << 8);
  const uint16_t ry = (aData[11] << 4) | (aData[10] >> 4);

  auto withinDeadzone = [](uint16_t aX, uint16_t aY) {
    const double dx = static_cast<double>(aX) - kSwitchStickCenter;
    const double dy = static_cast<double>(aY) - kSwitchStickCenter;
    return dx * dx + dy * dy <
           static_cast<double>(kSwitchStickDeadzone) * kSwitchStickDeadzone;
  };
  const bool leftDead = withinDeadzone(lx, ly);
  const bool rightDead = withinDeadzone(rx, ry);

  auto setAxis = [&](uint32_t aIndex, double aValue) {
    if (aIndex < aGamepad.axes.Length() &&
        aGamepad.axes[aIndex].value != aValue) {
      service->NewAxisMoveEvent(aGamepad.gamepadHandle, aIndex, aValue);
      aGamepad.axes[aIndex].value = aValue;
    }
  };
  // The Standard Gamepad uses +down for the Y axes, so invert them.
  setAxis(AXIS_INDEX_LEFT_STICK_X, leftDead ? 0.0 : NormalizeSwitchAxis(lx));
  setAxis(AXIS_INDEX_LEFT_STICK_Y, leftDead ? 0.0 : -NormalizeSwitchAxis(ly));
  setAxis(AXIS_INDEX_RIGHT_STICK_X, rightDead ? 0.0 : NormalizeSwitchAxis(rx));
  setAxis(AXIS_INDEX_RIGHT_STICK_Y, rightDead ? 0.0 : -NormalizeSwitchAxis(ry));
}

bool WindowsGamepadService::HandleRawInput(HRAWINPUT handle) {
  if (!mHID) {
    return false;
  }

  RefPtr<GamepadPlatformService> service =
      GamepadPlatformService::GetParentService();
  if (!service) {
    return false;
  }

  // First, get data from the handle
  UINT size;
  GetRawInputData(handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
  nsTArray<uint8_t> data(size);
  data.SetLength(size);
  if (GetRawInputData(handle, RID_INPUT, data.Elements(), &size,
                      sizeof(RAWINPUTHEADER)) == kRawInputError) {
    return false;
  }
  PRAWINPUT raw = reinterpret_cast<PRAWINPUT>(data.Elements());

  Gamepad* gamepad = nullptr;
  for (unsigned i = 0; i < mGamepads.Length(); i++) {
    if (mGamepads[i].type == kRawInputGamepad &&
        mGamepads[i].handle == raw->header.hDevice) {
      gamepad = &mGamepads[i];
      break;
    }
  }
  if (gamepad == nullptr) {
    return false;
  }

  // The Switch Pro's report descriptor doesn't describe its 0x30 full report,
  // so parse it directly rather than via HidP_*.
  if (gamepad->vendorId == kSwitchProVendorId &&
      gamepad->productId == kSwitchProProductId &&
      raw->data.hid.dwSizeHid >= 12 &&
      raw->data.hid.bRawData[0] == kSwitchProFullReportId) {
    ParseSwitchProReport(*gamepad, raw->data.hid.bRawData);
    return true;
  }

  // Second, get the preparsed data
  nsTArray<uint8_t> parsedbytes;
  if (!GetPreparsedData(raw->header.hDevice, parsedbytes)) {
    return false;
  }
  PHIDP_PREPARSED_DATA parsed =
      reinterpret_cast<PHIDP_PREPARSED_DATA>(parsedbytes.Elements());

  // Get all the pressed buttons.
  nsTArray<USAGE> usages(gamepad->numButtons);
  usages.SetLength(gamepad->numButtons);
  ULONG usageLength = gamepad->numButtons;
  if (mHID.mHidP_GetUsages(HidP_Input, kButtonUsagePage, 0, usages.Elements(),
                           &usageLength, parsed, (PCHAR)raw->data.hid.bRawData,
                           raw->data.hid.dwSizeHid) != HIDP_STATUS_SUCCESS) {
    return false;
  }

  nsTArray<bool> buttons(gamepad->numButtons);
  buttons.SetLength(gamepad->numButtons);
  // If we don't zero out the buttons array first, sometimes it can reuse
  // values.
  memset(buttons.Elements(), 0, gamepad->numButtons * sizeof(bool));

  for (unsigned i = 0; i < usageLength; i++) {
    // The button index in usages may be larger than what we detected when
    // enumerating gamepads. If so, warn and continue.
    //
    // Usage ID of 0 is reserved, so it should always be 1 or higher.
    if (NS_WARN_IF((usages[i] - 1u) >= buttons.Length())) {
      continue;
    }
    buttons[usages[i] - 1u] = true;
  }

  for (unsigned i = 0; i < gamepad->numButtons; i++) {
    if (gamepad->buttons[i] != buttons[i]) {
      gamepad->remapper->RemapButtonEvent(gamepad->gamepadHandle, i,
                                          buttons[i]);
      gamepad->buttons[i] = buttons[i];
    }
  }

  // Get all axis values.
  for (unsigned i = 0; i < gamepad->numAxes; i++) {
    double new_value;
    if (gamepad->axes[i].caps.LogicalMin < 0) {
      LONG value;
      if (mHID.mHidP_GetScaledUsageValue(
              HidP_Input, gamepad->axes[i].caps.UsagePage, 0,
              gamepad->axes[i].caps.Range.UsageMin, &value, parsed,
              (PCHAR)raw->data.hid.bRawData,
              raw->data.hid.dwSizeHid) != HIDP_STATUS_SUCCESS) {
        continue;
      }
      new_value = ScaleAxis(value, gamepad->axes[i].caps.LogicalMin,
                            gamepad->axes[i].caps.LogicalMax);
    } else {
      ULONG value;
      if (mHID.mHidP_GetUsageValue(
              HidP_Input, gamepad->axes[i].caps.UsagePage, 0,
              gamepad->axes[i].caps.Range.UsageMin, &value, parsed,
              (PCHAR)raw->data.hid.bRawData,
              raw->data.hid.dwSizeHid) != HIDP_STATUS_SUCCESS) {
        continue;
      }

      new_value = ScaleAxis(value, gamepad->axes[i].caps.LogicalMin,
                            gamepad->axes[i].caps.LogicalMax);
    }
    if (gamepad->axes[i].value != new_value) {
      gamepad->remapper->RemapAxisMoveEvent(gamepad->gamepadHandle, i,
                                            new_value);
      gamepad->axes[i].value = new_value;
    }
  }

  gamepad->remapper->ProcessTouchData(
      gamepad->gamepadHandle, raw->data.hid.bRawData, raw->data.hid.dwSizeHid);

  return true;
}

void WindowsGamepadService::SetLightIndicatorColor(
    const Tainted<GamepadHandle>& aGamepadHandle,
    const Tainted<uint32_t>& aLightColorIndex, const uint8_t& aRed,
    const uint8_t& aGreen, const uint8_t& aBlue) {
  // We get aControllerIdx from GamepadPlatformService::AddGamepad(),
  // It begins from 1 and is stored at Gamepad.id.
  const Gamepad* gamepad = (MOZ_FIND_AND_VALIDATE(
      aGamepadHandle, list_item.gamepadHandle == aGamepadHandle, mGamepads));
  if (!gamepad) {
    MOZ_ASSERT(false);
    return;
  }

  RefPtr<GamepadRemapper> remapper = gamepad->remapper;
  if (!remapper ||
      MOZ_IS_VALID(aLightColorIndex,
                   remapper->GetLightIndicatorCount() <= aLightColorIndex)) {
    MOZ_ASSERT(false);
    return;
  }

  std::vector<uint8_t> report;
  remapper->GetLightColorReport(aRed, aGreen, aBlue, report);
  WriteOutputReport(report);
}

size_t WindowsGamepadService::WriteOutputReport(
    const std::vector<uint8_t>& aReport) {
  DCHECK(static_cast<const void*>(aReport.data()));
  DCHECK_GE(aReport.size(), 1U);
  if (!mHidHandle) return 0;

  nsAutoHandle eventHandle(::CreateEvent(nullptr, FALSE, FALSE, nullptr));
  OVERLAPPED overlapped = {0};
  overlapped.hEvent = eventHandle;

  // Doing an asynchronous write to allows us to time out
  // if the write takes too long.
  DWORD bytesWritten = 0;
  BOOL writeSuccess =
      ::WriteFile(mHidHandle, static_cast<const void*>(aReport.data()),
                  aReport.size(), &bytesWritten, &overlapped);
  if (!writeSuccess) {
    DWORD error = ::GetLastError();
    if (error == ERROR_IO_PENDING) {
      // Wait for the write to complete. This causes WriteOutputReport to behave
      // synchronously but with a timeout.
      DWORD wait_object = ::WaitForSingleObject(overlapped.hEvent, 100);
      if (wait_object == WAIT_OBJECT_0) {
        if (!::GetOverlappedResult(mHidHandle, &overlapped, &bytesWritten,
                                   TRUE)) {
          return 0;
        }
      } else {
        // Wait failed, or the timeout was exceeded before the write completed.
        // Cancel the write request.
        if (::CancelIo(mHidHandle)) {
          wait_object = ::WaitForSingleObject(overlapped.hEvent, INFINITE);
          MOZ_ASSERT(wait_object == WAIT_OBJECT_0);
        }
      }
    }
  }
  return writeSuccess ? bytesWritten : 0;
}

void WindowsGamepadService::Startup() { ScanForDevices(); }

void WindowsGamepadService::Shutdown() { Cleanup(); }

void WindowsGamepadService::Cleanup() {
  mIsXInputMonitoring = false;
  if (mDirectInputTimer) {
    mDirectInputTimer->Cancel();
  }
  if (mXInputTimer) {
    mXInputTimer->Cancel();
  }
  if (mDeviceChangeTimer) {
    mDeviceChangeTimer->Cancel();
  }

  mGamepads.Clear();
}

void WindowsGamepadService::DevicesChanged(bool aIsStablizing) {
  if (aIsStablizing) {
    mDeviceChangeTimer->Cancel();
    mDeviceChangeTimer->InitWithNamedFuncCallback(
        DevicesChangeCallback, this, kDevicesChangedStableDelay,
        nsITimer::TYPE_ONE_SHOT, "DevicesChangeCallback"_ns);
  } else {
    ScanForDevices();
  }
}

bool RegisterRawInput(HWND hwnd, bool enable) {
  nsTArray<RAWINPUTDEVICE> rid(std::size(kUsagePages));
  rid.SetLength(std::size(kUsagePages));

  for (unsigned i = 0; i < rid.Length(); i++) {
    rid[i].usUsagePage = kUsagePages[i].usagePage;
    rid[i].usUsage = kUsagePages[i].usage;
    rid[i].dwFlags =
        enable ? RIDEV_EXINPUTSINK | RIDEV_DEVNOTIFY : RIDEV_REMOVE;
    rid[i].hwndTarget = hwnd;
  }

  if (!RegisterRawInputDevices(rid.Elements(), rid.Length(),
                               sizeof(RAWINPUTDEVICE))) {
    return false;
  }
  return true;
}

static LRESULT CALLBACK GamepadWindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                          LPARAM lParam) {
  const unsigned int DBT_DEVICEARRIVAL = 0x8000;
  const unsigned int DBT_DEVICEREMOVECOMPLETE = 0x8004;
  const unsigned int DBT_DEVNODES_CHANGED = 0x7;

  switch (msg) {
    case WM_DEVICECHANGE:
      if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE ||
          wParam == DBT_DEVNODES_CHANGED) {
        if (gService) {
          gService->DevicesChanged(true);
        }
      }
      break;
    case WM_INPUT:
      if (gService) {
        gService->HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
      }
      break;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

class StartWindowsGamepadServiceRunnable final : public Runnable {
 public:
  StartWindowsGamepadServiceRunnable()
      : Runnable("StartWindowsGamepadServiceRunnable") {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_GetCurrentThread() == gMonitorThread);
    gService = new WindowsGamepadService();
    gService->Startup();

    if (sHWnd == nullptr) {
      WNDCLASSW wc;
      HMODULE hSelf = GetModuleHandle(nullptr);

      if (!GetClassInfoW(hSelf, L"MozillaGamepadClass", &wc)) {
        ZeroMemory(&wc, sizeof(WNDCLASSW));
        wc.hInstance = hSelf;
        wc.lpfnWndProc = GamepadWindowProc;
        wc.lpszClassName = L"MozillaGamepadClass";
        RegisterClassW(&wc);
      }

      sHWnd = CreateWindowW(L"MozillaGamepadClass", L"Gamepad Watcher", 0, 0, 0,
                            0, 0, nullptr, nullptr, hSelf, nullptr);
      RegisterRawInput(sHWnd, true);
    }

    // Explicitly start the message loop
    gService->StartMessageLoop();

    return NS_OK;
  }

 private:
  ~StartWindowsGamepadServiceRunnable() = default;
};

class StopWindowsGamepadServiceRunnable final : public Runnable {
 public:
  StopWindowsGamepadServiceRunnable()
      : Runnable("StopWindowsGamepadServiceRunnable") {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_GetCurrentThread() == gMonitorThread);
    if (sHWnd) {
      RegisterRawInput(sHWnd, false);
      DestroyWindow(sHWnd);
      sHWnd = nullptr;
    }

    gService->Shutdown();
    delete gService;
    gService = nullptr;

    return NS_OK;
  }

 private:
  ~StopWindowsGamepadServiceRunnable() = default;
};

}  // namespace

namespace mozilla::dom {

using namespace mozilla::ipc;

void StartGamepadMonitoring() {
  AssertIsOnBackgroundThread();

  if (gMonitorThread || gService) {
    return;
  }
  sIsShutdown = false;
  NS_NewNamedThread("Gamepad", getter_AddRefs(gMonitorThread));
  gMonitorThread->Dispatch(new StartWindowsGamepadServiceRunnable(),
                           NS_DISPATCH_NORMAL);
}

void StopGamepadMonitoring() {
  AssertIsOnBackgroundThread();

  if (sIsShutdown) {
    return;
  }
  sIsShutdown = true;
  gMonitorThread->Dispatch(new StopWindowsGamepadServiceRunnable(),
                           NS_DISPATCH_NORMAL);
  gMonitorThread->Shutdown();
  gMonitorThread = nullptr;
}

void SetGamepadLightIndicatorColor(const Tainted<GamepadHandle>& aGamepadHandle,
                                   const Tainted<uint32_t>& aLightColorIndex,
                                   const uint8_t& aRed, const uint8_t& aGreen,
                                   const uint8_t& aBlue) {
  MOZ_ASSERT(gService);
  if (!gService) {
    return;
  }
  gService->SetLightIndicatorColor(aGamepadHandle, aLightColorIndex, aRed,
                                   aGreen, aBlue);
}

}  // namespace mozilla::dom
