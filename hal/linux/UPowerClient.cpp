/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Hal.h"
#include "HalLog.h"
#include <mozilla/Attributes.h>
#include <mozilla/dom/battery/Constants.h>
#include "mozilla/GRefPtr.h"
#include "mozilla/GUniquePtr.h"
#include <cmath>
#include <gio/gio.h>
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/widget/AsyncDBus.h"
#include "nsAppShell.h"

using namespace mozilla::widget;
using namespace mozilla::dom::battery;

namespace mozilla::hal_impl {

/**
 * This is the declaration of UPowerClient class. This class is listening and
 * communicating to upower daemon through DBus.
 * There is no header file because this class shouldn't be public.
 */
class UPowerClient {
 public:
  static UPowerClient* GetInstance();

  void BeginListening();
  void StopListening();

  double GetLevel();
  bool IsCharging();
  double GetRemainingTime();

  ~UPowerClient();

 private:
  UPowerClient();

  enum States {
    eState_Unknown = 0,
    eState_Charging,
    eState_Discharging,
    eState_Empty,
    eState_FullyCharged,
    eState_PendingCharge,
    eState_PendingDischarge
  };

  /**
   * Update the currently tracked device.
   */
  void UpdateTrackedDevices();

  /**
   * Update the battery info.
   */
  bool GetBatteryInfo();

  /**
   * Watch battery device for status
   */
  bool AddTrackedDevice(const char* devicePath);

  /**
   * Callback used by 'DeviceChanged' signal.
   */
  static void DeviceChanged(GDBusProxy* aProxy, gchar* aSenderName,
                            gchar* aSignalName, GVariant* aParameters,
                            UPowerClient* aListener);

  /**
   * Callback used by 'PropertiesChanged' signal.
   * This method is called when the the battery level changes.
   * (Only with upower >= 0.99)
   */
  static void DevicePropertiesChanged(GDBusProxy* aProxy, gchar* aSenderName,
                                      gchar* aSignalName, GVariant* aParameters,
                                      UPowerClient* aListener);

  RefPtr<GCancellable> mCancellable;

  // The DBus proxy object to upower.
  RefPtr<GDBusProxy> mUPowerProxy;

  // The path of the tracked device.
  GUniquePtr<gchar> mTrackedDevice;

  // The DBusGProxy for the tracked device.
  RefPtr<GDBusProxy> mTrackedDeviceProxy;

  double mLevel;
  bool mCharging;
  double mRemainingTime;

  static UPowerClient* sInstance;

  static const guint sDeviceTypeBattery = 2;
  static const guint64 kUPowerUnknownRemainingTime = 0;
};

/*
 * Implementation of mozilla::hal_impl::EnableBatteryNotifications,
 *                   mozilla::hal_impl::DisableBatteryNotifications,
 *               and mozilla::hal_impl::GetCurrentBatteryInformation.
 */

void EnableBatteryNotifications() {
  if (!StaticPrefs::dom_battery_enabled()) {
    return;
  }
  UPowerClient::GetInstance()->BeginListening();
}

void DisableBatteryNotifications() {
  UPowerClient::GetInstance()->StopListening();
}

void GetCurrentBatteryInformation(hal::BatteryInformation* aBatteryInfo) {
  UPowerClient* upowerClient = UPowerClient::GetInstance();

  aBatteryInfo->level() = upowerClient->GetLevel();
  aBatteryInfo->charging() = upowerClient->IsCharging();
  aBatteryInfo->remainingTime() = upowerClient->GetRemainingTime();
}

/*
 * Following is the implementation of UPowerClient.
 */

UPowerClient* UPowerClient::sInstance = nullptr;

/* static */
UPowerClient* UPowerClient::GetInstance() {
  if (!sInstance) {
    sInstance = new UPowerClient();
  }

  return sInstance;
}

UPowerClient::UPowerClient()
    : mLevel(kDefaultLevel),
      mCharging(kDefaultCharging),
      mRemainingTime(kDefaultRemainingTime) {}

UPowerClient::~UPowerClient() {
  NS_ASSERTION(
      !mUPowerProxy && !mTrackedDevice && !mTrackedDeviceProxy && !mCancellable,
      "The observers have not been correctly removed! "
      "(StopListening should have been called)");
}

void UPowerClient::BeginListening() {
  GUniquePtr<GError> error;

  mCancellable = dont_AddRef(g_cancellable_new());
  CreateDBusProxyForBus(G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE,
                        /* aInterfaceInfo = */ nullptr,
                        "org.freedesktop.UPower", "/org/freedesktop/UPower",
                        "org.freedesktop.UPower", mCancellable)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          // It's safe to capture this as we use mCancellable to stop
          // listening.
          [this](RefPtr<GDBusProxy>&& aProxy) {
            mUPowerProxy = std::move(aProxy);
            UpdateTrackedDevices();
          },
          [](GUniquePtr<GError>&& aError) {
            if (!g_error_matches(aError.get(), G_IO_ERROR,
                                 G_IO_ERROR_CANCELLED)) {
              g_warning(
                  "Failed to create DBus proxy for org.freedesktop.UPower: "
                  "%s\n",
                  aError->message);
            }
          });
}

void UPowerClient::StopListening() {
  if (mUPowerProxy) {
    g_signal_handlers_disconnect_by_func(mUPowerProxy, (void*)DeviceChanged,
                                         this);
  }
  if (mCancellable) {
    g_cancellable_cancel(mCancellable);
    mCancellable = nullptr;
  }

  mTrackedDeviceProxy = nullptr;
  mTrackedDevice = nullptr;
  mUPowerProxy = nullptr;

  // We should now show the default values, not the latest we got.
  mLevel = kDefaultLevel;
  mCharging = kDefaultCharging;
  mRemainingTime = kDefaultRemainingTime;
}

bool UPowerClient::AddTrackedDevice(const char* aDevicePath) {
  nsAppShell::DBusConnectionCheck();
  RefPtr<GDBusProxy> proxy = dont_AddRef(g_dbus_proxy_new_for_bus_sync(
      G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, nullptr,
      "org.freedesktop.UPower", aDevicePath, "org.freedesktop.UPower.Device",
      mCancellable, nullptr));
  if (!proxy) {
    return false;
  }

  RefPtr<GVariant> deviceType =
      dont_AddRef(g_dbus_proxy_get_cached_property(proxy, "Type"));
  if (NS_WARN_IF(!deviceType ||
                 !g_variant_is_of_type(deviceType, G_VARIANT_TYPE_UINT32))) {
    return false;
  }

  if (g_variant_get_uint32(deviceType) != sDeviceTypeBattery) {
    return false;
  }

  GUniquePtr<gchar> device(g_strdup(aDevicePath));
  mTrackedDevice = std::move(device);
  mTrackedDeviceProxy = std::move(proxy);

  if (!GetBatteryInfo()) {
    return false;
  }
  hal::NotifyBatteryChange(
      hal::BatteryInformation(mLevel, mCharging, mRemainingTime));

  g_signal_connect(mTrackedDeviceProxy, "g-signal",
                   G_CALLBACK(DevicePropertiesChanged), this);
  return true;
}

void UPowerClient::UpdateTrackedDevices() {
  // Reset the current tracked device:
  g_signal_handlers_disconnect_by_func(mUPowerProxy, (void*)DeviceChanged,
                                       this);

  mTrackedDevice = nullptr;
  mTrackedDeviceProxy = nullptr;

  nsAppShell::DBusConnectionCheck();
  DBusProxyCall(mUPowerProxy, "EnumerateDevices", nullptr,
                G_DBUS_CALL_FLAGS_NONE, -1, mCancellable)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          // It's safe to capture this as we use mCancellable to stop
          // listening.
          [this](RefPtr<GVariant>&& aResult) {
            RefPtr<GVariant> variant =
                dont_AddRef(g_variant_get_child_value(aResult.get(), 0));
            if (!variant || !g_variant_is_of_type(
                                variant, G_VARIANT_TYPE_OBJECT_PATH_ARRAY)) {
              g_warning(
                  "Failed to enumerate devices of org.freedesktop.UPower: "
                  "wrong param %s\n",
                  g_variant_get_type_string(aResult.get()));
              return;
            }
            gsize num = g_variant_n_children(variant);
            for (gsize i = 0; i < num; i++) {
              const char* devicePath = g_variant_get_string(
                  g_variant_get_child_value(variant, i), nullptr);
              if (!devicePath) {
                g_warning(
                    "Failed to enumerate devices of org.freedesktop.UPower: "
                    "missing device?\n");
                return;
              }
              /*
               * We are looking for the first device that is a battery.
               * TODO: we could try to combine more than one battery.
               */
              if (AddTrackedDevice(devicePath)) {
                break;
              }
            }
            g_signal_connect(mUPowerProxy, "g-signal",
                             G_CALLBACK(DeviceChanged), this);
          },
          [this](GUniquePtr<GError>&& aError) {
            if (!g_error_matches(aError.get(), G_IO_ERROR,
                                 G_IO_ERROR_CANCELLED)) {
              g_warning(
                  "Failed to enumerate devices of org.freedesktop.UPower: %s\n",
                  aError->message);
            }
            g_signal_connect(mUPowerProxy, "g-signal",
                             G_CALLBACK(DeviceChanged), this);
          });
}

/* static */
void UPowerClient::DeviceChanged(GDBusProxy* aProxy, gchar* aSenderName,
                                 gchar* aSignalName, GVariant* aParameters,
                                 UPowerClient* aListener) {
  // Added new device. Act only if we're missing any tracked device
  if (!g_strcmp0(aSignalName, "DeviceAdded")) {
    if (aListener->mTrackedDevice) {
      return;
    }
  } else if (!g_strcmp0(aSignalName, "DeviceRemoved")) {
    if (g_strcmp0(aSenderName, aListener->mTrackedDevice.get())) {
      return;
    }
  }
  aListener->UpdateTrackedDevices();
}

/* static */
void UPowerClient::DevicePropertiesChanged(GDBusProxy* aProxy,
                                           gchar* aSenderName,
                                           gchar* aSignalName,
                                           GVariant* aParameters,
                                           UPowerClient* aListener) {
  if (aListener->GetBatteryInfo()) {
    hal::NotifyBatteryChange(hal::BatteryInformation(
        sInstance->mLevel, sInstance->mCharging, sInstance->mRemainingTime));
  }
}

bool UPowerClient::GetBatteryInfo() {
  bool isFull = false;

  /*
   * State values are confusing...
   * First of all, after looking at upower sources (0.9.13), it seems that
   * PendingDischarge and PendingCharge are not used.
   * In addition, FullyCharged and Empty states are not clear because we do not
   * know if the battery is actually charging or not. Those values come directly
   * from sysfs (in the Linux kernel) which have four states: "Empty", "Full",
   * "Charging" and "Discharging". In sysfs, "Empty" and "Full" are also only
   * related to the level, not to the charging state.
   * In this code, we are going to assume that Full means charging and Empty
   * means discharging because if that is not the case, the state should not
   * last a long time (actually, it should disappear at the following update).
   * It might be even very hard to see real cases where the state is Empty and
   * the battery is charging or the state is Full and the battery is discharging
   * given that plugging/unplugging the battery should have an impact on the
   * level.
   */

  if (!mTrackedDeviceProxy) {
    return false;
  }

  RefPtr<GVariant> value = dont_AddRef(
      g_dbus_proxy_get_cached_property(mTrackedDeviceProxy, "State"));
  if (NS_WARN_IF(!value ||
                 !g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32))) {
    return false;
  }

  switch (g_variant_get_uint32(value)) {
    case eState_Unknown:
      mCharging = kDefaultCharging;
      break;
    case eState_FullyCharged:
      isFull = true;
      [[fallthrough]];
    case eState_Charging:
    case eState_PendingCharge:
      mCharging = true;
      break;
    case eState_Discharging:
    case eState_Empty:
    case eState_PendingDischarge:
      mCharging = false;
      break;
  }

  /*
   * The battery level might be very close to 100% (like 99%) without
   * increasing. It seems that upower sets the battery state as 'full' in that
   * case so we should trust it and not even try to get the value.
   */
  if (isFull) {
    mLevel = 1.0;
  } else {
    value = dont_AddRef(
        g_dbus_proxy_get_cached_property(mTrackedDeviceProxy, "Percentage"));
    if (NS_WARN_IF(!value ||
                   !g_variant_is_of_type(value, G_VARIANT_TYPE_DOUBLE))) {
      return false;
    }
    mLevel = round(g_variant_get_double(value)) * 0.01;
  }

  if (isFull) {
    mRemainingTime = 0;
  } else {
    value = dont_AddRef(g_dbus_proxy_get_cached_property(
        mTrackedDeviceProxy, mCharging ? "TimeToFull" : "TimeToEmpty"));
    if (NS_WARN_IF(!value ||
                   !g_variant_is_of_type(value, G_VARIANT_TYPE_INT64))) {
      return false;
    }
    mRemainingTime = g_variant_get_int64(value);
    if (mRemainingTime == kUPowerUnknownRemainingTime) {
      mRemainingTime = kUnknownRemainingTime;
    }
  }
  return true;
}

double UPowerClient::GetLevel() { return mLevel; }

bool UPowerClient::IsCharging() { return mCharging; }

double UPowerClient::GetRemainingTime() { return mRemainingTime; }

}  // namespace mozilla::hal_impl
