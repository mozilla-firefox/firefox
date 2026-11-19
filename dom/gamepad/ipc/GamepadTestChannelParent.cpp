/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GamepadTestChannelParent.h"

#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/GamepadPlatformService.h"
#include "mozilla/ipc/BackgroundParent.h"

namespace mozilla::dom {

already_AddRefed<GamepadTestChannelParent> GamepadTestChannelParent::Create() {
  // Refuse to create the parent actor if this pref is disabled
  if (!StaticPrefs::dom_gamepad_test_enabled()) {
    return nullptr;
  }

  return RefPtr<GamepadTestChannelParent>(new GamepadTestChannelParent())
      .forget();
}

GamepadTestChannelParent::GamepadTestChannelParent() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  GamepadMonitoringState::GetSingleton().AddObserver(this);
}

GamepadTestChannelParent::~GamepadTestChannelParent() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  GamepadMonitoringState::GetSingleton().RemoveObserver(this);
}

void GamepadTestChannelParent::AddGamepadToPlatformService(
    uint32_t aPromiseId, const GamepadAdded& aGamepadAdded) {
  mozilla::ipc::AssertIsOnBackgroundThread();

  RefPtr<GamepadPlatformService> service =
      GamepadPlatformService::GetParentService();
  MOZ_ASSERT(service);

  const GamepadAdded& a = aGamepadAdded;
  nsCString gamepadID;
  LossyCopyUTF16toASCII(a.id(), gamepadID);
  GamepadHandle handle = service->AddGamepad(
      gamepadID.get(), static_cast<GamepadMappingType>(a.mapping()), a.hand(),
      a.num_buttons(), a.num_axes(), a.num_haptics(), a.num_lights(),
      a.num_touches());

  (void)SendReplyGamepadHandle(aPromiseId, handle);
}

void GamepadTestChannelParent::OnMonitoringStateChanged(bool aNewState) {
  mozilla::ipc::AssertIsOnBackgroundThread();

  if (aNewState) {
    for (auto& deferredGamepadAdd : mDeferredGamepadAdded) {
      AddGamepadToPlatformService(deferredGamepadAdd.promiseId,
                                  deferredGamepadAdd.gamepadAdded);
    }
    mDeferredGamepadAdded.Clear();
  }
}

mozilla::ipc::IPCResult GamepadTestChannelParent::RecvGamepadTestEvent(
    const uint32_t& aID, const GamepadChangeEvent& aEvent) {
  mozilla::ipc::AssertIsOnBackgroundThread();

  RefPtr<GamepadPlatformService> service =
      GamepadPlatformService::GetParentService();
  MOZ_ASSERT(service);

  const GamepadChangeEventBody& body = aEvent.body();

  // GamepadAdded is special because it will be deferred if monitoring hasn't
  // started. Everything else won't be deferred and will fail if monitoring
  // isn't running
  if (body.type() == GamepadChangeEventBody::TGamepadAdded) {
    if (GamepadMonitoringState::GetSingleton().IsMonitoring()) {
      AddGamepadToPlatformService(aID, body.get_GamepadAdded());
    } else {
      mDeferredGamepadAdded.AppendElement(
          DeferredGamepadAdded{aID, body.get_GamepadAdded()});
    }
    return IPC_OK();
  }

  if (!GamepadMonitoringState::GetSingleton().IsMonitoring()) {
    return IPC_FAIL(this, "Simulated message received while not monitoring");
  }

  GamepadHandle handle = aEvent.handle();

  switch (body.type()) {
    case GamepadChangeEventBody::TGamepadRemoved:
      service->RemoveGamepad(handle);
      break;
    case GamepadChangeEventBody::TGamepadButtonInformation: {
      const GamepadButtonInformation& a = body.get_GamepadButtonInformation();
      service->NewButtonEvent(handle, a.button(), a.pressed(), a.touched(),
                              a.value());
      break;
    }
    case GamepadChangeEventBody::TGamepadAxisInformation: {
      const GamepadAxisInformation& a = body.get_GamepadAxisInformation();
      service->NewAxisMoveEvent(handle, a.axis(), a.value());
      break;
    }
    case GamepadChangeEventBody::TGamepadPoseInformation: {
      const GamepadPoseInformation& a = body.get_GamepadPoseInformation();
      service->NewPoseEvent(handle, a.pose_state());
      break;
    }
    case GamepadChangeEventBody::TGamepadTouchInformation: {
      const GamepadTouchInformation& a = body.get_GamepadTouchInformation();
      service->NewMultiTouchEvent(handle, a.index(), a.touch_state());
      break;
    }
    default:
      NS_WARNING("Unknown event type.");
      return IPC_FAIL_NO_REASON(this);
  }
  (void)SendReplyGamepadHandle(aID, handle);
  return IPC_OK();
}

}  // namespace mozilla::dom
