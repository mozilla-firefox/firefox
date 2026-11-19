/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * LinuxGamepadService: An evdev backend for the GamepadService.
 *
 * Ref: https://www.kernel.org/doc/html/latest/input/gamepad.html
 */
#include <glib.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <unordered_map>

#include "mozilla/Sprintf.h"
#include "mozilla/Tainting.h"
#include "mozilla/UdevLib.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/GamepadHandle.h"
#include "mozilla/dom/GamepadPlatformService.h"
#include "mozilla/dom/GamepadRemapping.h"
#include "nscore.h"

#define BITS_PER_LONG ((sizeof(unsigned long)) * 8)
#define BITS_TO_LONGS(x) (((x) + BITS_PER_LONG - 1) / BITS_PER_LONG)

namespace {

using namespace mozilla::dom;
using mozilla::udev_device;
using mozilla::udev_enumerate;
using mozilla::udev_lib;
using mozilla::udev_list_entry;
using mozilla::udev_monitor;
using mozilla::UniquePtr;

static const char kEvdevPath[] = "/dev/input/event";

static inline bool TestBit(const unsigned long* arr, size_t bit) {
  return !!(arr[bit / BITS_PER_LONG] & (1LL << (bit % BITS_PER_LONG)));
}

static inline double ScaleAxis(const input_absinfo& info, int value) {
  return 2.0 * (value - info.minimum) / (double)(info.maximum - info.minimum) -
         1.0;
}

// TODO: should find a USB identifier for each device so we can
// provide something that persists across connect/disconnect cycles.
struct Gamepad {
  GamepadHandle handle;
  bool isStandardGamepad = false;
  RefPtr<GamepadRemapper> remapper = nullptr;
  guint source_id = UINT_MAX;
  char idstring[256] = {0};
  char devpath[PATH_MAX] = {0};
  uint8_t key_map[KEY_CNT] = {0};
  uint8_t abs_map[ABS_CNT] = {0};
  std::unordered_map<uint16_t, input_absinfo> abs_info;
};

// evdev admits key/abs codes up to KEY_MAX/ABS_MAX inclusive, so the maps must
// have KEY_CNT/ABS_CNT (== *_MAX + 1) entries to index every valid code.
static_assert(sizeof(Gamepad::key_map) > KEY_MAX,
              "key_map must hold every valid EV_KEY code (0..KEY_MAX)");
static_assert(sizeof(Gamepad::abs_map) > ABS_MAX,
              "abs_map must hold every valid EV_ABS code (0..ABS_MAX)");

static inline bool LoadAbsInfo(int fd, Gamepad* gamepad, uint16_t code) {
  input_absinfo info{0};
  if (ioctl(fd, EVIOCGABS(code), &info) < 0) {
    return false;
  }
  if (info.minimum == info.maximum) {
    return false;
  }
  gamepad->abs_info.emplace(code, std::move(info));
  return true;
}

class LinuxGamepadService {
 public:
  LinuxGamepadService() : mMonitor(nullptr), mMonitorSourceID(0) {}

  void Startup();
  void Shutdown();

 private:
  void AddDevice(struct udev_device* dev);
  void RemoveDevice(struct udev_device* dev);
  void ScanForDevices();
  void AddMonitor();
  void RemoveMonitor();
  bool IsDeviceGamepad(struct udev_device* dev);
  bool IsXboxDevice(struct udev_device* aDev);
  void ReadUdevChange();

  // handler for data from /dev/input/eventN
  static gboolean OnGamepadData(GIOChannel* source, GIOCondition condition,
                                gpointer data);

  // handler for data from udev monitor
  static gboolean OnUdevMonitor(GIOChannel* source, GIOCondition condition,
                                gpointer data);

  udev_lib mUdev;
  struct udev_monitor* mMonitor;
  guint mMonitorSourceID;
  // Information about currently connected gamepads.
  AutoTArray<UniquePtr<Gamepad>, 4> mGamepads;
};

// singleton instance
LinuxGamepadService* gService MOZ_GUARDED_BY(mozilla::sMainThreadCapability) =
    nullptr;

void LinuxGamepadService::AddDevice(struct udev_device* dev) {
  RefPtr<GamepadPlatformService> service =
      GamepadPlatformService::GetParentService();
  if (!service) {
    return;
  }

  const char* devpath = mUdev.udev_device_get_devnode(dev);
  if (!devpath) {
    return;
  }

  // Ensure that this device hasn't already been added.
  for (unsigned int i = 0; i < mGamepads.Length(); i++) {
    if (strcmp(mGamepads[i]->devpath, devpath) == 0) {
      return;
    }
  }

  auto gamepad = std::make_unique<Gamepad>();
  snprintf(gamepad->devpath, sizeof(gamepad->devpath), "%s", devpath);
  GIOChannel* channel = g_io_channel_new_file(devpath, "r", nullptr);
  if (!channel) {
    return;
  }

  g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, nullptr);
  g_io_channel_set_encoding(channel, nullptr, nullptr);
  g_io_channel_set_buffered(channel, FALSE);
  int fd = g_io_channel_unix_get_fd(channel);

  input_id id{0};
  if (ioctl(fd, EVIOCGID, &id) < 0) {
    return;
  }

  char name[128]{0};
  if (ioctl(fd, EVIOCGNAME(sizeof(name)), &name) < 0) {
    strcpy(name, "Unknown Device");
  }

  SprintfLiteral(gamepad->idstring, "%04" PRIx16 "-%04" PRIx16 "-%s", id.vendor,
                 id.product, name);

  unsigned long keyBits[BITS_TO_LONGS(KEY_CNT)] = {0};
  unsigned long absBits[BITS_TO_LONGS(ABS_CNT)] = {0};
  if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0 ||
      ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) < 0) {
    return;
  }

  /* Here, we try to support even strange cases where proper semantic
   * BTN_GAMEPAD button are combined with arbitrary extra buttons. */

  /* These are mappings where the index is a CanonicalButtonIndex and the value
   * is an evdev code */
  const std::array<uint16_t, BUTTON_INDEX_COUNT> kStandardButtons = {
      /* BUTTON_INDEX_PRIMARY = */ BTN_SOUTH,
      /* BUTTON_INDEX_SECONDARY = */ BTN_EAST,
      /* BUTTON_INDEX_TERTIARY = */ BTN_WEST,
      /* BUTTON_INDEX_QUATERNARY = */ BTN_NORTH,
      /* BUTTON_INDEX_LEFT_SHOULDER = */ BTN_TL,
      /* BUTTON_INDEX_RIGHT_SHOULDER = */ BTN_TR,
      /* BUTTON_INDEX_LEFT_TRIGGER = */ BTN_TL2,
      /* BUTTON_INDEX_RIGHT_TRIGGER = */ BTN_TR2,
      /* BUTTON_INDEX_BACK_SELECT = */ BTN_SELECT,
      /* BUTTON_INDEX_START = */ BTN_START,
      /* BUTTON_INDEX_LEFT_THUMBSTICK = */ BTN_THUMBL,
      /* BUTTON_INDEX_RIGHT_THUMBSTICK = */ BTN_THUMBR,
      /* BUTTON_INDEX_DPAD_UP = */ BTN_DPAD_UP,
      /* BUTTON_INDEX_DPAD_DOWN = */ BTN_DPAD_DOWN,
      /* BUTTON_INDEX_DPAD_LEFT = */ BTN_DPAD_LEFT,
      /* BUTTON_INDEX_DPAD_RIGHT = */ BTN_DPAD_RIGHT,
      /* BUTTON_INDEX_META = */ BTN_MODE,
  };
  const std::array<uint16_t, AXIS_INDEX_COUNT> kStandardAxes = {
      /* AXIS_INDEX_LEFT_STICK_X = */ ABS_X,
      /* AXIS_INDEX_LEFT_STICK_Y = */ ABS_Y,
      /* AXIS_INDEX_RIGHT_STICK_X = */ ABS_RX,
      /* AXIS_INDEX_RIGHT_STICK_Y = */ ABS_RY,
  };

  /*
   * According to https://www.kernel.org/doc/html/latest/input/gamepad.html,
   * "All gamepads that follow the protocol described here map BTN_GAMEPAD",
   * so we can use it as a proxy for semantic buttons in general. If it's
   * enabled, we're probably going to be acting like a standard gamepad
   */
  uint32_t numButtons = 0;
  if (TestBit(keyBits, BTN_GAMEPAD)) {
    gamepad->isStandardGamepad = true;
    for (uint8_t button = 0; button < BUTTON_INDEX_COUNT; button++) {
      gamepad->key_map[kStandardButtons[button]] = button;
    }

    if (IsXboxDevice(dev)) {
      // The `xpad` and `microsoft` drivers for Xbox like gamepads map the
      // button labelled X to BTN_X (== BTN_NORTH) and the button labelled Y to
      // BTN_Y (== BTN_WEST). However, unlike Nintendo controllers (where X is
      // north and Y is west), Xbox gamepads have X on the left (west) and Y on
      // the top (north). We swap them here to provide consistent results to the
      // Gamepad API.

      std::swap(gamepad->key_map[BTN_WEST], gamepad->key_map[BTN_NORTH]);
    }

    numButtons = BUTTON_INDEX_COUNT;
  }

  // Now, go through the non-semantic buttons and handle them as extras
  for (uint16_t key = 0; key < KEY_CNT; key++) {
    // Skip standard buttons
    if (gamepad->isStandardGamepad &&
        std::find(kStandardButtons.begin(), kStandardButtons.end(), key) !=
            kStandardButtons.end()) {
      continue;
    }

    if (TestBit(keyBits, key)) {
      gamepad->key_map[key] = numButtons++;
    }
  }

  uint32_t numAxes = 0;
  if (gamepad->isStandardGamepad) {
    for (uint8_t i = 0; i < AXIS_INDEX_COUNT; i++) {
      gamepad->abs_map[kStandardAxes[i]] = i;
      LoadAbsInfo(fd, gamepad.get(), kStandardAxes[i]);
    }
    numAxes = AXIS_INDEX_COUNT;

    // These are not real axis and get remapped to buttons.
    LoadAbsInfo(fd, gamepad.get(), ABS_HAT0X);
    LoadAbsInfo(fd, gamepad.get(), ABS_HAT0Y);
  }

  for (uint16_t i = 0; i < ABS_CNT; ++i) {
    if (gamepad->isStandardGamepad &&
        (std::find(kStandardAxes.begin(), kStandardAxes.end(), i) !=
             kStandardAxes.end() ||
         i == ABS_HAT0X || i == ABS_HAT0Y)) {
      continue;
    }

    if (TestBit(absBits, i)) {
      if (LoadAbsInfo(fd, gamepad.get(), i)) {
        gamepad->abs_map[i] = numAxes++;
      }
    }
  }

  // If this is a standard gamepad and has exactly 2 extra axes that are mapped
  // to ABS_Z and ABS_RZ, then those are analog triggers and will be remapped.
  if (gamepad->isStandardGamepad && numAxes == (AXIS_INDEX_COUNT + 2) &&
      gamepad->abs_map[ABS_Z] && gamepad->abs_map[ABS_RZ]) {
    // Correct the number of axes reported.
    numAxes -= 2;

    // Disable discrete trigger buttons. Set BTN_TL2 and BTN_TR2 to an invalid
    // value that does not exist on the gamepad, so they won't trigger a button
    // press when you partially pull the trigger.
    gamepad->key_map[BTN_TL2] = 255;
    gamepad->key_map[BTN_TR2] = 255;
  }

  if (numAxes == 0) {
    NS_WARNING("Gamepad with zero axes detected?");
  }
  if (numButtons == 0) {
    NS_WARNING("Gamepad with zero buttons detected?");
  }

  // NOTE: This almost always true, so we basically never use the remapping
  // code.
  if (gamepad->isStandardGamepad) {
    gamepad->handle =
        service->AddGamepad(gamepad->idstring, GamepadMappingType::Standard,
                            GamepadHand::_empty, numButtons, numAxes, 0, 0, 0);
  } else {
    bool defaultRemapper = false;
    RefPtr<GamepadRemapper> remapper =
        GetGamepadRemapper(id.vendor, id.product, defaultRemapper);
    MOZ_ASSERT(remapper);
    remapper->SetAxisCount(numAxes);
    remapper->SetButtonCount(numButtons);

    gamepad->handle = service->AddGamepad(
        gamepad->idstring, remapper->GetMappingType(), GamepadHand::_empty,
        remapper->GetButtonCount(), remapper->GetAxisCount(), 0,
        remapper->GetLightIndicatorCount(), remapper->GetTouchEventCount());
    gamepad->remapper = remapper.forget();
  }
  // TODO: Bug 680289, implement gamepad haptics for Linux.
  // TODO: Bug 1523355, implement gamepad lighindicator and touch for Linux.

  gamepad->source_id =
      g_io_add_watch(channel, GIOCondition(G_IO_IN | G_IO_ERR | G_IO_HUP),
                     OnGamepadData, gamepad.get());
  g_io_channel_unref(channel);

  mGamepads.AppendElement(std::move(gamepad));
}

void LinuxGamepadService::RemoveDevice(struct udev_device* dev) {
  RefPtr<GamepadPlatformService> service =
      GamepadPlatformService::GetParentService();
  if (!service) {
    return;
  }

  const char* devpath = mUdev.udev_device_get_devnode(dev);
  if (!devpath) {
    return;
  }

  for (unsigned int i = 0; i < mGamepads.Length(); i++) {
    if (strcmp(mGamepads[i]->devpath, devpath) == 0) {
      auto gamepad = std::move(mGamepads[i]);
      mGamepads.RemoveElementAt(i);

      g_source_remove(gamepad->source_id);
      service->RemoveGamepad(gamepad->handle);

      break;
    }
  }
}

void LinuxGamepadService::ScanForDevices() {
  struct udev_enumerate* en = mUdev.udev_enumerate_new(mUdev.udev);
  mUdev.udev_enumerate_add_match_subsystem(en, "input");
  mUdev.udev_enumerate_scan_devices(en);

  struct udev_list_entry* dev_list_entry;
  for (dev_list_entry = mUdev.udev_enumerate_get_list_entry(en);
       dev_list_entry != nullptr;
       dev_list_entry = mUdev.udev_list_entry_get_next(dev_list_entry)) {
    const char* path = mUdev.udev_list_entry_get_name(dev_list_entry);
    struct udev_device* dev =
        mUdev.udev_device_new_from_syspath(mUdev.udev, path);
    if (IsDeviceGamepad(dev)) {
      AddDevice(dev);
    }

    mUdev.udev_device_unref(dev);
  }

  mUdev.udev_enumerate_unref(en);
}

void LinuxGamepadService::AddMonitor() {
  // Add a monitor to watch for device changes
  mMonitor = mUdev.udev_monitor_new_from_netlink(mUdev.udev, "udev");
  if (!mMonitor) {
    // Not much we can do here.
    return;
  }
  mUdev.udev_monitor_filter_add_match_subsystem_devtype(mMonitor, "input",
                                                        nullptr);

  int monitor_fd = mUdev.udev_monitor_get_fd(mMonitor);
  GIOChannel* monitor_channel = g_io_channel_unix_new(monitor_fd);
  mMonitorSourceID = g_io_add_watch(monitor_channel,
                                    GIOCondition(G_IO_IN | G_IO_ERR | G_IO_HUP),
                                    OnUdevMonitor, nullptr);
  g_io_channel_unref(monitor_channel);

  mUdev.udev_monitor_enable_receiving(mMonitor);
}

void LinuxGamepadService::RemoveMonitor() {
  if (mMonitorSourceID) {
    g_source_remove(mMonitorSourceID);
    mMonitorSourceID = 0;
  }
  if (mMonitor) {
    mUdev.udev_monitor_unref(mMonitor);
    mMonitor = nullptr;
  }
}

void LinuxGamepadService::Startup() {
  // Don't bother starting up if libudev couldn't be loaded or initialized.
  if (!mUdev) {
    return;
  }

  AddMonitor();
  ScanForDevices();
}

void LinuxGamepadService::Shutdown() {
  for (unsigned int i = 0; i < mGamepads.Length(); i++) {
    g_source_remove(mGamepads[i]->source_id);
  }
  mGamepads.Clear();
  RemoveMonitor();
}

bool LinuxGamepadService::IsDeviceGamepad(struct udev_device* aDev) {
  if (!mUdev.udev_device_get_property_value(aDev, "ID_INPUT_JOYSTICK")) {
    return false;
  }

  const char* devpath = mUdev.udev_device_get_devnode(aDev);
  if (!devpath) {
    return false;
  }

  return strncmp(devpath, kEvdevPath, strlen(kEvdevPath)) == 0;
}

bool LinuxGamepadService::IsXboxDevice(struct udev_device* aDev) {
  const char* driver = nullptr;
  struct udev_device* p = mUdev.udev_device_get_parent(aDev);
  while (p && !driver) {
    driver = mUdev.udev_device_get_driver(p);
    p = mUdev.udev_device_get_parent(p);
  }
  if (!driver) {
    return false;
  }
  if (strcmp(driver, "xpad") == 0) {
    // Wired Xbox controllers and Xbox 360 wireless receivers
    return true;
  }
  if (strcmp(driver, "microsoft") == 0) {
    // Bluetooth Xbox controllers
    return true;
  }
  return false;
}

void LinuxGamepadService::ReadUdevChange() {
  struct udev_device* dev = mUdev.udev_monitor_receive_device(mMonitor);
  if (IsDeviceGamepad(dev)) {
    const char* action = mUdev.udev_device_get_action(dev);
    if (strcmp(action, "add") == 0) {
      AddDevice(dev);
    } else if (strcmp(action, "remove") == 0) {
      RemoveDevice(dev);
    }
  }
  mUdev.udev_device_unref(dev);
}

// static
gboolean LinuxGamepadService::OnGamepadData(GIOChannel* source,
                                            GIOCondition condition,
                                            gpointer data) {
  RefPtr<GamepadPlatformService> service =
      GamepadPlatformService::GetParentService();
  if (!service) {
    return TRUE;
  }
  auto* gamepad = static_cast<Gamepad*>(data);

  // TODO: remove gamepad?
  if (condition & (G_IO_ERR | G_IO_HUP)) {
    return FALSE;
  }

  while (true) {
    struct input_event event{};
    gsize count;
    GError* err = nullptr;
    if (g_io_channel_read_chars(source, (gchar*)&event, sizeof(event), &count,
                                &err) != G_IO_STATUS_NORMAL ||
        count == 0) {
      break;
    }

    switch (event.type) {
      case EV_KEY:
        // event.code comes from the device and is untrusted; bound it before
        // indexing key_map.
        if (event.code >= KEY_CNT) {
          continue;
        }
        if (gamepad->isStandardGamepad) {
          if (gamepad->key_map[event.code] == 255) {
            continue;
          }
          service->NewButtonEvent(gamepad->handle, gamepad->key_map[event.code],
                                  !!event.value);
        } else {
          gamepad->remapper->RemapButtonEvent(
              gamepad->handle, gamepad->key_map[event.code], !!event.value);
        }
        break;
      case EV_ABS: {
        if (!gamepad->abs_info.count(event.code)) {
          continue;
        }

        double scaledValue =
            ScaleAxis(gamepad->abs_info[event.code], event.value);
        if (gamepad->isStandardGamepad) {
          switch (event.code) {
            case ABS_HAT0X:
              service->NewButtonEvent(gamepad->handle, BUTTON_INDEX_DPAD_LEFT,
                                      AxisNegativeAsButton(scaledValue));
              service->NewButtonEvent(gamepad->handle, BUTTON_INDEX_DPAD_RIGHT,
                                      AxisPositiveAsButton(scaledValue));
              break;
            case ABS_HAT0Y:
              service->NewButtonEvent(gamepad->handle, BUTTON_INDEX_DPAD_UP,
                                      AxisNegativeAsButton(scaledValue));
              service->NewButtonEvent(gamepad->handle, BUTTON_INDEX_DPAD_DOWN,
                                      AxisPositiveAsButton(scaledValue));
              break;
            case ABS_Z: {
              const double value = AxisToButtonValue(scaledValue);
              service->NewButtonEvent(gamepad->handle,
                                      BUTTON_INDEX_LEFT_TRIGGER,
                                      value > BUTTON_THRESHOLD_VALUE, value);
              break;
            }
            case ABS_RZ: {
              const double value = AxisToButtonValue(scaledValue);
              service->NewButtonEvent(gamepad->handle,
                                      BUTTON_INDEX_RIGHT_TRIGGER,
                                      value > BUTTON_THRESHOLD_VALUE, value);
              break;
            }
            default:
              service->NewAxisMoveEvent(
                  gamepad->handle, gamepad->abs_map[event.code], scaledValue);
              break;
          }
        } else {
          gamepad->remapper->RemapAxisMoveEvent(
              gamepad->handle, gamepad->abs_map[event.code], scaledValue);
        }
      } break;
    }
  }

  return TRUE;
}

// static
gboolean LinuxGamepadService::OnUdevMonitor(GIOChannel* source,
                                            GIOCondition condition,
                                            gpointer data) {
  mozilla::AssertIsOnMainThread();

  if (!gService || condition & (G_IO_ERR | G_IO_HUP)) {
    return FALSE;
  }

  gService->ReadUdevChange();
  return TRUE;
}

}  // namespace

namespace mozilla::dom {

void StartGamepadMonitoring() {
  MOZ_ASSERT(!NS_IsMainThread());

  NS_DispatchToMainThread(NS_NewRunnableFunction("StartGamepadMonitoring", [] {
    AssertIsOnMainThread();
    if (!gService) {
      gService = new LinuxGamepadService();
      gService->Startup();
    }
  }));
}

void StopGamepadMonitoring() {
  MOZ_ASSERT(!NS_IsMainThread());

  NS_DispatchToMainThread(NS_NewRunnableFunction("StopGamepadMonitoring", [] {
    AssertIsOnMainThread();
    if (gService) {
      gService->Shutdown();
      delete gService;
      gService = nullptr;
    }
  }));
}

void SetGamepadLightIndicatorColor(const Tainted<GamepadHandle>& aGamepadHandle,
                                   const Tainted<uint32_t>& aLightColorIndex,
                                   const uint8_t& aRed, const uint8_t& aGreen,
                                   const uint8_t& aBlue) {
  // TODO: Bug 1523355.
  NS_WARNING("Linux doesn't support gamepad light indicator.");
}

}  // namespace mozilla::dom
