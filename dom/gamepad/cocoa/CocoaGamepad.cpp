/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// mostly derived from the Allegro source code at:
// http://alleg.svn.sourceforge.net/viewvc/alleg/allegro/branches/4.9/src/macosx/hidjoy.m?revision=13760&view=markup

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDBase.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDManager.h>

#include <vector>

#include "mozilla/Atomics.h"
#include "mozilla/Mutex.h"
#include "mozilla/Sprintf.h"
#include "mozilla/Tainting.h"
#include "mozilla/dom/GamepadHandle.h"
#include "mozilla/dom/GamepadPlatformService.h"
#include "mozilla/dom/GamepadRemapping.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "nsComponentManagerUtils.h"
#include "nsThreadUtils.h"

namespace {

using namespace mozilla;
using namespace mozilla::dom;
class DarwinGamepadService;

DarwinGamepadService* gService = nullptr;

struct Button {
  int id;
  bool analog;
  IOHIDElementRef element;
  CFIndex min;
  CFIndex max;
  bool pressed;

  Button(int aId, IOHIDElementRef aElement, CFIndex aMin, CFIndex aMax)
      : id(aId),
        analog((aMax - aMin) > 1),
        element(aElement),
        min(aMin),
        max(aMax),
        pressed(false) {}
};

struct Axis {
  int id;
  IOHIDElementRef element;
  uint32_t usagePage;
  uint32_t usage;
  CFIndex min;
  CFIndex max;
  double value;
};

// These values can be found in the USB HID Usage Tables:
// http://www.usb.org/developers/hidpage
const unsigned kDesktopUsagePage = 0x01;
const unsigned kSimUsagePage = 0x02;
const unsigned kAcceleratorUsage = 0xC4;
const unsigned kBrakeUsage = 0xC5;
const unsigned kJoystickUsage = 0x04;
const unsigned kGamepadUsage = 0x05;
const unsigned kAxisUsageMin = 0x30;
const unsigned kAxisUsageMax = 0x35;
const unsigned kDpadUsage = 0x39;
const unsigned kButtonUsagePage = 0x09;
const unsigned kConsumerPage = 0x0C;
const unsigned kHomeUsage = 0x223;
const unsigned kBackUsage = 0x224;

// Upper bound for one blocking pass of the monitor thread's run loop. IOKit
// wakes the loop as soon as it posts an event, so this only bounds how long a
// stop request that raced with entering the loop can be delayed.
const CFTimeInterval kDarwinGamepadRunLoopTimeout = 1.0;

struct GamepadInputReportContext {
  DarwinGamepadService* service;
  size_t gamepadSlot;
};

class Gamepad {
 private:
  IOHIDDeviceRef mDevice;
  nsTArray<Button> buttons;
  nsTArray<Axis> axes;

 public:
  Gamepad() : mDevice(nullptr) {}

  bool operator==(IOHIDDeviceRef device) const { return mDevice == device; }
  bool empty() const { return mDevice == nullptr; }
  IOHIDDeviceRef Device() const { return mDevice; }
  void clear() {
    mDevice = nullptr;
    buttons.Clear();
    axes.Clear();
    mHandle = GamepadHandle{};
  }
  void init(IOHIDDeviceRef device, bool defaultRemapper);
  void ReportChanged(uint8_t* report, CFIndex report_length);

  size_t numButtons() { return buttons.Length(); }
  size_t numAxes() { return axes.Length(); }

  Button* lookupButton(IOHIDElementRef element) {
    for (unsigned i = 0; i < buttons.Length(); i++) {
      if (buttons[i].element == element) return &buttons[i];
    }
    return nullptr;
  }

  Axis* lookupAxis(IOHIDElementRef element) {
    for (unsigned i = 0; i < axes.Length(); i++) {
      if (axes[i].element == element) return &axes[i];
    }
    return nullptr;
  }

  GamepadHandle mHandle;
  RefPtr<GamepadRemapper> mRemapper;
  nsTArray<uint8_t> mInputReport;
  UniquePtr<GamepadInputReportContext> mInputReportContext;
};

void Gamepad::init(IOHIDDeviceRef aDevice, bool aDefaultRemapper) {
  clear();
  mDevice = aDevice;

  CFArrayRef elements =
      IOHIDDeviceCopyMatchingElements(aDevice, nullptr, kIOHIDOptionsTypeNone);
  CFIndex n = CFArrayGetCount(elements);
  for (CFIndex i = 0; i < n; i++) {
    IOHIDElementRef element =
        (IOHIDElementRef)CFArrayGetValueAtIndex(elements, i);
    uint32_t usagePage = IOHIDElementGetUsagePage(element);
    uint32_t usage = IOHIDElementGetUsage(element);

    if (usagePage == kDesktopUsagePage && usage >= kAxisUsageMin &&
        usage <= kAxisUsageMax) {
      Axis axis = {aDefaultRemapper ? int(axes.Length())
                                    : static_cast<int>(usage - kAxisUsageMin),
                   element,
                   usagePage,
                   usage,
                   IOHIDElementGetLogicalMin(element),
                   IOHIDElementGetLogicalMax(element)};
      axes.AppendElement(axis);
    } else if (usagePage == kDesktopUsagePage && usage == kDpadUsage &&
               // Don't know how to handle d-pads that return weird values.
               IOHIDElementGetLogicalMax(element) -
                       IOHIDElementGetLogicalMin(element) ==
                   7) {
      Axis axis = {aDefaultRemapper ? int(axes.Length())
                                    : static_cast<int>(usage - kAxisUsageMin),
                   element,
                   usagePage,
                   usage,
                   IOHIDElementGetLogicalMin(element),
                   IOHIDElementGetLogicalMax(element)};
      axes.AppendElement(axis);
    } else if (usagePage == kSimUsagePage &&
               (usage == kAcceleratorUsage || usage == kBrakeUsage)) {
      if (IOHIDElementGetType(element) == kIOHIDElementTypeInput_Button) {
        Button button(int(buttons.Length()), element,
                      IOHIDElementGetLogicalMin(element),
                      IOHIDElementGetLogicalMax(element));
        buttons.AppendElement(button);
      } else {
        Axis axis = {aDefaultRemapper ? int(axes.Length())
                                      : static_cast<int>(usage - kAxisUsageMin),
                     element,
                     usagePage,
                     usage,
                     IOHIDElementGetLogicalMin(element),
                     IOHIDElementGetLogicalMax(element)};
        axes.AppendElement(axis);
      }
    } else if ((usagePage == kButtonUsagePage) ||
               (usagePage == kConsumerPage &&
                (usage == kHomeUsage || usage == kBackUsage))) {
      Button button(int(buttons.Length()), element,
                    IOHIDElementGetLogicalMin(element),
                    IOHIDElementGetLogicalMax(element));
      buttons.AppendElement(button);
    } else {
      // TODO: handle other usage pages
    }
  }
}

// This service is created and destroyed in Background thread while
// operates in a seperate thread(We call it Monitor thread here).
class DarwinGamepadService {
 private:
  IOHIDManagerRef mManager;
  Mutex mGamepadsMutex;
  nsTArray<Gamepad> mGamepads MOZ_GUARDED_BY(mGamepadsMutex);

  nsCOMPtr<nsIThread> mMonitorThread;
  nsCOMPtr<nsIThread> mBackgroundThread;
  // Retained run loop of the monitor thread, so that Shutdown() can stop it
  // from the background thread without racing its destruction.
  CFRunLoopRef mRunLoop MOZ_GUARDED_BY(mGamepadsMutex);
  Atomic<bool> mIsRunning;

  static void DeviceAddedCallback(void* data, IOReturn result, void* sender,
                                  IOHIDDeviceRef device);
  static void DeviceRemovedCallback(void* data, IOReturn result, void* sender,
                                    IOHIDDeviceRef device);
  static void InputValueChangedCallback(void* data, IOReturn result,
                                        void* sender, IOHIDValueRef newValue);

  void DeviceAdded(IOHIDDeviceRef device);
  void DeviceRemoved(IOHIDDeviceRef device);
  void InputValueChanged(IOHIDValueRef value);
  void StartupInternal();
  void RunEventLoopOnce();

 public:
  DarwinGamepadService();
  ~DarwinGamepadService();

  static void ReportChangedCallback(void* context, IOReturn result,
                                    void* sender, IOHIDReportType report_type,
                                    uint32_t report_id, uint8_t* report,
                                    CFIndex report_length);

  void Startup();
  void Shutdown();
  void SetLightIndicatorColor(const Tainted<GamepadHandle>& aGamepadHandle,
                              const Tainted<uint32_t>& aLightColorIndex,
                              const uint8_t& aRed, const uint8_t& aGreen,
                              const uint8_t& aBlue);
  friend class DarwinGamepadServiceStartupRunnable;
  friend class DarwinGamepadServiceShutdownRunnable;
};

class DarwinGamepadServiceStartupRunnable final : public Runnable {
 private:
  ~DarwinGamepadServiceStartupRunnable() = default;
  // This Runnable schedules startup of DarwinGamepadService
  // in a new thread, pointer to DarwinGamepadService is only
  // used by this Runnable within its thread.
  DarwinGamepadService MOZ_NON_OWNING_REF* mService;

 public:
  explicit DarwinGamepadServiceStartupRunnable(DarwinGamepadService* service)
      : Runnable("DarwinGamepadServiceStartupRunnable"), mService(service) {}
  NS_IMETHOD Run() override {
    MOZ_ASSERT(mService);
    mService->StartupInternal();
    return NS_OK;
  }
};

class DarwinGamepadServiceShutdownRunnable final : public Runnable {
 private:
  ~DarwinGamepadServiceShutdownRunnable() = default;

 public:
  // This Runnable schedules shutdown of DarwinGamepadService
  // in background thread.
  explicit DarwinGamepadServiceShutdownRunnable()
      : Runnable("DarwinGamepadServiceStartupRunnable") {}
  NS_IMETHOD Run() override {
    MOZ_ASSERT(gService);
    MOZ_ASSERT(NS_GetCurrentThread() == gService->mBackgroundThread);

    IOHIDManagerRef manager = (IOHIDManagerRef)gService->mManager;

    if (manager) {
      IOHIDManagerClose(manager, 0);
      CFRelease(manager);
      gService->mManager = nullptr;
    }
    gService->mMonitorThread->Shutdown();
    delete gService;
    gService = nullptr;
    return NS_OK;
  }
};

void DarwinGamepadService::DeviceAdded(IOHIDDeviceRef device) {
  RefPtr<GamepadPlatformService> service =
      GamepadPlatformService::GetParentService();
  if (!service) {
    return;
  }

  // Holding the lock through IOHIDDeviceRegisterInputReportCallback is safe:
  // ReportChangedCallback (which also acquires this lock) is only ever invoked
  // from within CFRunLoopRunInMode on the monitor thread, which is not running
  // while DeviceAdded is executing.
  MutexAutoLock lock(mGamepadsMutex);

  size_t slot = size_t(-1);
  for (size_t i = 0; i < mGamepads.Length(); i++) {
    if (mGamepads[i] == device) return;
    if (slot == size_t(-1) && mGamepads[i].empty()) slot = i;
  }

  if (slot == size_t(-1)) {
    slot = mGamepads.Length();
    mGamepads.AppendElement(Gamepad());
  }

  // Gather some identifying information
  auto uintProperty = [&](CFStringRef key) {
    int value;
    auto numberRef =
        reinterpret_cast<CFNumberRef>(IOHIDDeviceGetProperty(device, key));
    if (!numberRef || !CFNumberGetValue(numberRef, kCFNumberIntType, &value)) {
      return 0u;
    }
    return static_cast<unsigned int>(value);
  };

  unsigned int vendorId = uintProperty(CFSTR(kIOHIDVendorIDKey));
  unsigned int productId = uintProperty(CFSTR(kIOHIDProductIDKey));

  char productName[128];
  CFStringRef productRef =
      (CFStringRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey));
  if (!productRef ||
      !CFStringGetCString(productRef, productName, sizeof(productName),
                          kCFStringEncodingASCII)) {
    SprintfLiteral(productName, "Unknown Device");
  }

  char buffer[256];
  SprintfLiteral(buffer, "%04x-%04x-%s", vendorId, productId, productName);

  bool defaultRemapper = false;
  RefPtr<GamepadRemapper> remapper =
      GetGamepadRemapper(vendorId, productId, defaultRemapper);
  MOZ_ASSERT(remapper);
  mGamepads[slot].init(device, defaultRemapper);

  remapper->SetAxisCount(mGamepads[slot].numAxes());
  remapper->SetButtonCount(mGamepads[slot].numButtons());

  GamepadHandle handle = service->AddGamepad(
      buffer, remapper->GetMappingType(), mozilla::dom::GamepadHand::_empty,
      remapper->GetButtonCount(), remapper->GetAxisCount(),
      0,  // TODO: Bug 680289, implement gamepad haptics for cocoa.
      remapper->GetLightIndicatorCount(), remapper->GetTouchEventCount());

  nsTArray<GamepadLightIndicatorType> lightTypes;
  remapper->GetLightIndicators(lightTypes);
  for (uint32_t i = 0; i < lightTypes.Length(); ++i) {
    if (lightTypes[i] != GamepadLightIndicator::DefaultType()) {
      service->NewLightIndicatorTypeEvent(handle, i, lightTypes[i]);
    }
  }

  mGamepads[slot].mHandle = handle;
  mGamepads[slot].mInputReport.SetLength(remapper->GetMaxInputReportLength());
  mGamepads[slot].mInputReportContext = UniquePtr<GamepadInputReportContext>(
      new GamepadInputReportContext{this, slot});
  mGamepads[slot].mRemapper = remapper.forget();

  IOHIDDeviceRegisterInputReportCallback(
      device, mGamepads[slot].mInputReport.Elements(),
      mGamepads[slot].mInputReport.Length(), ReportChangedCallback,
      mGamepads[slot].mInputReportContext.get());
}

void DarwinGamepadService::DeviceRemoved(IOHIDDeviceRef device) {
  RefPtr<GamepadPlatformService> service =
      GamepadPlatformService::GetParentService();
  if (!service) {
    return;
  }
  // Same reasoning as DeviceAdded: holding the lock through
  // IOHIDDeviceRegisterInputReportCallback (the unregister call) is safe
  // because ReportChangedCallback only runs from within CFRunLoopRunInMode on
  // the monitor thread, which is not running while DeviceRemoved executes.
  MutexAutoLock lock(mGamepadsMutex);
  for (Gamepad& gamepad : mGamepads) {
    if (gamepad == device) {
      IOHIDDeviceRegisterInputReportCallback(
          device, gamepad.mInputReport.Elements(), 0, nullptr,
          gamepad.mInputReportContext.get());

      gamepad.mInputReportContext.reset();

      service->RemoveGamepad(gamepad.mHandle);
      gamepad.clear();
      return;
    }
  }
}

// Replace context to be Gamepad.
void DarwinGamepadService::ReportChangedCallback(
    void* context, IOReturn result, void* sender, IOHIDReportType report_type,
    uint32_t report_id, uint8_t* report, CFIndex report_length) {
  if (context && report_type == kIOHIDReportTypeInput && report_length) {
    auto reportContext = static_cast<GamepadInputReportContext*>(context);
    DarwinGamepadService* service = reportContext->service;
    MutexAutoLock lock(service->mGamepadsMutex);
    service->mGamepads[reportContext->gamepadSlot].ReportChanged(report,
                                                                 report_length);
  }
}

void Gamepad::ReportChanged(uint8_t* report, CFIndex report_len) {
  MOZ_RELEASE_ASSERT(report_len <= mRemapper->GetMaxInputReportLength());
  mRemapper->ProcessTouchData(mHandle, report, report_len);
}

void DarwinGamepadService::InputValueChanged(IOHIDValueRef value) {
  RefPtr<GamepadPlatformService> service =
      GamepadPlatformService::GetParentService();
  if (!service) {
    return;
  }

  uint32_t value_length = IOHIDValueGetLength(value);
  if (value_length > 4) {
    // Workaround for bizarre issue with PS3 controllers that try to return
    // massive (30+ byte) values and crash IOHIDValueGetIntegerValue
    return;
  }
  IOHIDElementRef element = IOHIDValueGetElement(value);
  IOHIDDeviceRef device = IOHIDElementGetDevice(element);

  MutexAutoLock lock(mGamepadsMutex);
  for (Gamepad& gamepad : mGamepads) {
    if (gamepad == device) {
      // Axis elements represent axes and d-pads.
      if (Axis* axis = gamepad.lookupAxis(element)) {
        const double d = IOHIDValueGetIntegerValue(value);
        const double v =
            2.0f * (d - axis->min) / (double)(axis->max - axis->min) - 1.0f;
        if (axis->value != v) {
          gamepad.mRemapper->RemapAxisMoveEvent(gamepad.mHandle, axis->id, v);
          axis->value = v;
        }
      } else if (Button* button = gamepad.lookupButton(element)) {
        const int iv = IOHIDValueGetIntegerValue(value);
        const bool pressed = iv != 0;
        if (button->pressed != pressed) {
          gamepad.mRemapper->RemapButtonEvent(gamepad.mHandle, button->id,
                                              pressed);
          button->pressed = pressed;
        }
      }
      return;
    }
  }
}

void DarwinGamepadService::DeviceAddedCallback(void* data, IOReturn result,
                                               void* sender,
                                               IOHIDDeviceRef device) {
  DarwinGamepadService* service = (DarwinGamepadService*)data;
  service->DeviceAdded(device);
}

void DarwinGamepadService::DeviceRemovedCallback(void* data, IOReturn result,
                                                 void* sender,
                                                 IOHIDDeviceRef device) {
  DarwinGamepadService* service = (DarwinGamepadService*)data;
  service->DeviceRemoved(device);
}

void DarwinGamepadService::InputValueChangedCallback(void* data,
                                                     IOReturn result,
                                                     void* sender,
                                                     IOHIDValueRef newValue) {
  DarwinGamepadService* service = (DarwinGamepadService*)data;
  service->InputValueChanged(newValue);
}

static CFMutableDictionaryRef MatchingDictionary(UInt32 inUsagePage,
                                                 UInt32 inUsage) {
  CFMutableDictionaryRef dict = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  if (!dict) return nullptr;
  CFNumberRef number =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &inUsagePage);
  if (!number) {
    CFRelease(dict);
    return nullptr;
  }
  CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsagePageKey), number);
  CFRelease(number);

  number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &inUsage);
  if (!number) {
    CFRelease(dict);
    return nullptr;
  }
  CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsageKey), number);
  CFRelease(number);

  return dict;
}

DarwinGamepadService::DarwinGamepadService()
    : mManager(nullptr),
      mGamepadsMutex("DarwinGamepadService::mGamepads"),
      mRunLoop(nullptr),
      mIsRunning(false) {}

DarwinGamepadService::~DarwinGamepadService() {
  if (mManager != nullptr) CFRelease(mManager);
  mMonitorThread = nullptr;
  mBackgroundThread = nullptr;
  MutexAutoLock lock(mGamepadsMutex);
  if (mRunLoop) {
    CFRelease(mRunLoop);
    mRunLoop = nullptr;
  }
}

void DarwinGamepadService::RunEventLoopOnce() {
  MOZ_ASSERT(NS_GetCurrentThread() == mMonitorThread);
  if (mIsRunning) {
    SInt32 result = CFRunLoopRunInMode(kCFRunLoopDefaultMode,
                                       kDarwinGamepadRunLoopTimeout, false);
    // An empty mode means the IOHIDManager source is gone and no input can
    // arrive anymore. Not expected, but tear down rather than spin.
    if (mIsRunning && result != kCFRunLoopRunFinished) {
      NS_DispatchToCurrentThread(NewNonOwningRunnableMethod(
          "DarwinGamepadService::RunEventLoopOnce", this,
          &DarwinGamepadService::RunEventLoopOnce));
      return;
    }
    mIsRunning = false;
  }

  // We schedule a task shutdown and cleaning up resources to Background
  // thread here to make sure no runloop is running to prevent potential race
  // condition.
  RefPtr<Runnable> shutdownTask = new DarwinGamepadServiceShutdownRunnable();
  mBackgroundThread->Dispatch(shutdownTask.forget(), NS_DISPATCH_NORMAL);
}

void DarwinGamepadService::StartupInternal() {
  if (mManager != nullptr) return;

  {
    MutexAutoLock lock(mGamepadsMutex);
    if (!mRunLoop) {
      mRunLoop = CFRunLoopGetCurrent();
      CFRetain(mRunLoop);
    }
  }

  IOHIDManagerRef manager =
      IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);

  CFMutableDictionaryRef criteria_arr[2];
  criteria_arr[0] = MatchingDictionary(kDesktopUsagePage, kJoystickUsage);
  if (!criteria_arr[0]) {
    CFRelease(manager);
    return;
  }

  criteria_arr[1] = MatchingDictionary(kDesktopUsagePage, kGamepadUsage);
  if (!criteria_arr[1]) {
    CFRelease(criteria_arr[0]);
    CFRelease(manager);
    return;
  }

  CFArrayRef criteria = CFArrayCreate(kCFAllocatorDefault,
                                      (const void**)criteria_arr, 2, nullptr);
  if (!criteria) {
    CFRelease(criteria_arr[1]);
    CFRelease(criteria_arr[0]);
    CFRelease(manager);
    return;
  }

  IOHIDManagerSetDeviceMatchingMultiple(manager, criteria);
  CFRelease(criteria);
  CFRelease(criteria_arr[1]);
  CFRelease(criteria_arr[0]);

  IOHIDManagerRegisterDeviceMatchingCallback(manager, DeviceAddedCallback,
                                             this);
  IOHIDManagerRegisterDeviceRemovalCallback(manager, DeviceRemovedCallback,
                                            this);
  IOHIDManagerRegisterInputValueCallback(manager, InputValueChangedCallback,
                                         this);
  IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(),
                                  kCFRunLoopDefaultMode);
  IOReturn rv = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
  if (rv != kIOReturnSuccess) {
    CFRelease(manager);
    return;
  }

  mManager = manager;

  mIsRunning = true;
  RunEventLoopOnce();
}

void DarwinGamepadService::Startup() {
  mBackgroundThread = NS_GetCurrentThread();
  (void)NS_NewNamedThread("Gamepad", getter_AddRefs(mMonitorThread),
                          new DarwinGamepadServiceStartupRunnable(this));
}

void DarwinGamepadService::Shutdown() {
  // Flipping this flag will stop the eventloop in Monitor thread
  // and dispatch a task destroying and cleaning up resources in
  // Background thread. Stopping the run loop wakes the Monitor thread
  // early if it is blocked waiting for input.
  mIsRunning = false;
  MutexAutoLock lock(mGamepadsMutex);
  if (mRunLoop) {
    CFRunLoopStop(mRunLoop);
  }
}

void DarwinGamepadService::SetLightIndicatorColor(
    const Tainted<GamepadHandle>& aGamepadHandle,
    const Tainted<uint32_t>& aLightColorIndex, const uint8_t& aRed,
    const uint8_t& aGreen, const uint8_t& aBlue) {
  // We get aControllerIdx from GamepadPlatformService::AddGamepad(),
  // It begins from 1 and is stored at Gamepad.id.
  IOHIDDeviceRef device = nullptr;
  std::vector<uint8_t> report;
  {
    MutexAutoLock lock(mGamepadsMutex);
    // the tainting macro confuses clang
    MOZ_PUSH_IGNORE_THREAD_SAFETY
    const Gamepad* gamepad = MOZ_FIND_AND_VALIDATE(
        aGamepadHandle, list_item.mHandle == aGamepadHandle, mGamepads);
    MOZ_POP_THREAD_SAFETY
    if (!gamepad) {
      MOZ_ASSERT(false);
      return;
    }

    RefPtr<GamepadRemapper> remapper = gamepad->mRemapper;
    if (!remapper ||
        MOZ_IS_VALID(aLightColorIndex,
                     remapper->GetLightIndicatorCount() <= aLightColorIndex)) {
      MOZ_ASSERT(false);
      return;
    }

    remapper->GetLightColorReport(aRed, aGreen, aBlue, report);
    device = gamepad->Device();
  }
  if (device) {
    IOReturn success =
        IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, report[0],
                             report.data(), report.size());
    MOZ_ASSERT(success == kIOReturnSuccess);
    (void)success;
  }
}

}  // namespace

namespace mozilla {
namespace dom {

void StartGamepadMonitoring() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  if (gService) {
    return;
  }

  gService = new DarwinGamepadService();
  gService->Startup();
}

void StopGamepadMonitoring() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  if (!gService) {
    return;
  }

  // Calling Shutdown() will delete gService as well
  gService->Shutdown();
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

}  // namespace dom
}  // namespace mozilla
