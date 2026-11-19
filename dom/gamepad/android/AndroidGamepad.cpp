/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// TODO: Bug 680289, implement gamepad haptics for Android.
// TODO: Bug 1523355, implement gamepad lighindicator and touch for
// Android.

#include "mozilla/Tainting.h"
#include "mozilla/dom/Gamepad.h"
#include "mozilla/dom/GamepadHandle.h"
#include "mozilla/dom/GamepadPlatformService.h"
#include "mozilla/java/AndroidGamepadManagerNatives.h"
#include "mozilla/java/GeckoAppShellWrappers.h"
#include "nsThreadUtils.h"

using mozilla::dom::GamepadHandle;

namespace mozilla {
namespace dom {

class AndroidGamepadManager final
    : public java::AndroidGamepadManager::Natives<AndroidGamepadManager> {
  AndroidGamepadManager() = delete;

 public:
  static jni::ByteArray::LocalRef NativeAddGamepad(jni::String::Param aName) {
    RefPtr<GamepadPlatformService> service =
        GamepadPlatformService::GetParentService();
    if (!service) {
      return nullptr;
    }

    nsCString name = aName->ToCString();
    if (name.IsEmpty()) {
      name.AssignLiteral("android");
    }

    const GamepadHandle gamepadHandle = service->AddGamepad(
        name.get(), GamepadMappingType::Standard, GamepadHand::_empty,
        kStandardGamepadButtons, kStandardGamepadAxes, 0, 0, 0);

    return mozilla::jni::ByteArray::New(
        reinterpret_cast<const int8_t*>(&gamepadHandle), sizeof(gamepadHandle));
  }

  static void NativeRemoveGamepad(jni::ByteArray::Param aGamepadHandleBytes) {
    GamepadHandle handle = JNIByteArrayToGamepadHandle(aGamepadHandleBytes);

    RefPtr<GamepadPlatformService> service =
        GamepadPlatformService::GetParentService();
    if (!service) {
      return;
    }

    service->RemoveGamepad(handle);
  }

  static void OnButtonChange(jni::ByteArray::Param aGamepadHandleBytes,
                             int32_t aButton, bool aPressed, float aValue) {
    GamepadHandle handle = JNIByteArrayToGamepadHandle(aGamepadHandleBytes);

    RefPtr<GamepadPlatformService> service =
        GamepadPlatformService::GetParentService();
    if (!service) {
      return;
    }

    service->NewButtonEvent(handle, aButton, aPressed, aValue);
  }

  static void OnAxisChange(jni::ByteArray::Param aGamepadHandleBytes,
                           jni::BooleanArray::Param aValid,
                           jni::FloatArray::Param aValues) {
    GamepadHandle handle = JNIByteArrayToGamepadHandle(aGamepadHandleBytes);

    RefPtr<GamepadPlatformService> service =
        GamepadPlatformService::GetParentService();
    if (!service) {
      return;
    }

    const auto& valid = aValid->GetElements();
    const auto& values = aValues->GetElements();
    MOZ_ASSERT(valid.Length() == values.Length());

    for (size_t i = 0; i < values.Length(); i++) {
      if (valid[i]) {
        service->NewAxisMoveEvent(handle, i, values[i]);
      }
    }
  }

 private:
  static GamepadHandle JNIByteArrayToGamepadHandle(
      jni::ByteArray::Param aGamepadHandleBytes) {
    MOZ_ASSERT(aGamepadHandleBytes->Length() == sizeof(GamepadHandle));

    GamepadHandle gamepadHandle;
    aGamepadHandleBytes->CopyTo(reinterpret_cast<int8_t*>(&gamepadHandle),
                                sizeof(gamepadHandle));

    return gamepadHandle;
  }
};

void StartGamepadMonitoring() {
  AndroidGamepadManager::Init();
  java::AndroidGamepadManager::Start(
      java::GeckoAppShell::GetApplicationContext());
}

void StopGamepadMonitoring() {
  java::AndroidGamepadManager::Stop(
      java::GeckoAppShell::GetApplicationContext());
}

void SetGamepadLightIndicatorColor(const Tainted<GamepadHandle>& aGamepadHandle,
                                   const Tainted<uint32_t>& aLightColorIndex,
                                   const uint8_t& aRed, const uint8_t& aGreen,
                                   const uint8_t& aBlue) {
  NS_WARNING("Android doesn't support gamepad light indicator.");
}

}  // namespace dom
}  // namespace mozilla
