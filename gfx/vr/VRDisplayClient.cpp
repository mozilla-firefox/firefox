/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <math.h>

#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/Gamepad.h"
#include "mozilla/dom/GamepadHandle.h"
#include "mozilla/dom/GamepadManager.h"
#include "mozilla/dom/WebXRBinding.h"
#include "mozilla/dom/XRInputSourceArray.h"
#include "mozilla/dom/XRSession.h"
#include "nsIGlobalObject.h"
#include "nsRefPtrHashtable.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "prenv.h"
#include "prlink.h"

#ifdef XP_WIN
#  include "../layers/d3d11/CompositorD3D11.h"
#endif

#include "VRDisplayClient.h"
#include "VRDisplayPresentation.h"
#include "VRLayerChild.h"
#include "VRManagerChild.h"

using namespace mozilla;
using namespace mozilla::gfx;

using mozilla::dom::GamepadHandle;
using mozilla::dom::GamepadHandleKind;

VRDisplayClient::VRDisplayClient(const VRDisplayInfo& aDisplayInfo)
    : mDisplayInfo(aDisplayInfo),
      bLastEventWasMounted(false),
      bLastEventWasPresenting(false),
      mPresentationCount(0),
      mLastEventFrameId(0),
      mLastPresentingGeneration(0),
      mLastEventControllerState{},
      // For now WebVR is default to prevent a VRDisplay restore bug in WebVR
      // compatibility mode. See Bug 1630512
      mAPIMode(VRAPIMode::WebVR) {
  MOZ_COUNT_CTOR(VRDisplayClient);
}

VRDisplayClient::~VRDisplayClient() { MOZ_COUNT_DTOR(VRDisplayClient); }

void VRDisplayClient::UpdateDisplayInfo(const VRDisplayInfo& aDisplayInfo) {
  mDisplayInfo = aDisplayInfo;
  FireEvents();
}

already_AddRefed<VRDisplayPresentation> VRDisplayClient::BeginPresentation(
    const nsTArray<mozilla::dom::VRLayer>& aLayers, uint32_t aGroup) {
  PresentationCreated();
  RefPtr<VRDisplayPresentation> presentation =
      new VRDisplayPresentation(this, aLayers, aGroup);
  return presentation.forget();
}

void VRDisplayClient::PresentationCreated() { ++mPresentationCount; }

void VRDisplayClient::PresentationDestroyed() { --mPresentationCount; }

void VRDisplayClient::SessionStarted(dom::XRSession* aSession) {
  PresentationCreated();
  MakePresentationGenerationCurrent();
  mSessions.AppendElement(aSession);
}
void VRDisplayClient::SessionEnded(dom::XRSession* aSession) {
  mSessions.RemoveElement(aSession);
  PresentationDestroyed();
}

void VRDisplayClient::StartFrame() {
  RefPtr<VRManagerChild> vm = VRManagerChild::Get();
  vm->RunFrameRequestCallbacks();

  nsTArray<RefPtr<dom::XRSession>> sessions;
  sessions.AppendElements(mSessions);
  for (auto session : sessions) {
    session->StartFrame();
  }
}

void VRDisplayClient::SetGroupMask(uint32_t aGroupMask) {
  VRManagerChild* vm = VRManagerChild::Get();
  vm->SendSetGroupMask(mDisplayInfo.mDisplayID, aGroupMask);
}

bool VRDisplayClient::IsPresentationGenerationCurrent() const {
  if (mLastPresentingGeneration !=
      mDisplayInfo.mDisplayState.presentingGeneration) {
    return false;
  }

  return true;
}

void VRDisplayClient::MakePresentationGenerationCurrent() {
  mLastPresentingGeneration = mDisplayInfo.mDisplayState.presentingGeneration;
}

gfx::VRAPIMode VRDisplayClient::GetXRAPIMode() const { return mAPIMode; }

void VRDisplayClient::SetXRAPIMode(gfx::VRAPIMode aMode) { mAPIMode = aMode; }

void VRDisplayClient::FireEvents() {
  RefPtr<VRManagerChild> vm = VRManagerChild::Get();
  // Only fire these events for non-chrome VR sessions
  bool isPresenting = (mDisplayInfo.mPresentingGroups & kVRGroupContent) != 0;

  // Check if we need to trigger onVRDisplayPresentChange event
  if (bLastEventWasPresenting != isPresenting) {
    bLastEventWasPresenting = isPresenting;
    vm->FireDOMVRDisplayPresentChangeEvent(mDisplayInfo.mDisplayID);
  }

  // Check if we need to trigger onvrdisplayactivate event
  if (!bLastEventWasMounted && mDisplayInfo.mDisplayState.isMounted) {
    bLastEventWasMounted = true;
    if (StaticPrefs::dom_vr_autoactivate_enabled()) {
      vm->FireDOMVRDisplayMountedEvent(mDisplayInfo.mDisplayID);
    }
  }

  // Check if we need to trigger onvrdisplaydeactivate event
  if (bLastEventWasMounted && !mDisplayInfo.mDisplayState.isMounted) {
    bLastEventWasMounted = false;
    if (StaticPrefs::dom_vr_autoactivate_enabled()) {
      vm->FireDOMVRDisplayUnmountedEvent(mDisplayInfo.mDisplayID);
    }
  }

  if (mLastPresentingGeneration !=
      mDisplayInfo.mDisplayState.presentingGeneration) {
    mLastPresentingGeneration = mDisplayInfo.mDisplayState.presentingGeneration;
    vm->NotifyPresentationGenerationChanged(mDisplayInfo.mDisplayID);
  }

  // In WebXR spec, Gamepad instances returned by an XRInputSource's gamepad
  // attribute MUST NOT be included in the array returned by
  // navigator.getGamepads().
  if (mAPIMode == VRAPIMode::WebVR) {
    FireGamepadEvents();
  }
  // Update controller states into XRInputSourceArray.
  for (auto& session : mSessions) {
    dom::XRInputSourceArray* inputs = session->InputSources();
    if (inputs) {
      inputs->Update(session);
    }
  }

  // Check if we need to trigger VRDisplay.requestAnimationFrame
  if (mLastEventFrameId != mDisplayInfo.mFrameId) {
    mLastEventFrameId = mDisplayInfo.mFrameId;
    StartFrame();
  }
}

void VRDisplayClient::GamepadMappingForWebVR(
    VRControllerState& aControllerState) {
  auto triggerValue = aControllerState.triggerValue;
  const uint64_t buttonPressed = aControllerState.buttonPressed;
  const uint64_t buttonTouched = aControllerState.buttonTouched;

  auto SetTriggerValue = [&](uint64_t newSlot, uint64_t oldSlot) {
    aControllerState.triggerValue[newSlot] = triggerValue[oldSlot];
  };
  auto ShiftButtonBitForNewSlot = [&](uint64_t newSlot, uint64_t oldSlot,
                                      bool aIsTouch = false) {
    if (aIsTouch) {
      return ((buttonTouched & (1ULL << oldSlot)) != 0) * (1ULL << newSlot);
    }
    SetTriggerValue(newSlot, oldSlot);
    return ((buttonPressed & (1ULL << oldSlot)) != 0) * (1ULL << newSlot);
  };

  switch (aControllerState.type) {
    case VRControllerType::HTCVive:
      aControllerState.buttonPressed =
          ShiftButtonBitForNewSlot(1, 0) | ShiftButtonBitForNewSlot(2, 1) |
          ShiftButtonBitForNewSlot(0, 2) | ShiftButtonBitForNewSlot(3, 4);
      aControllerState.buttonTouched = ShiftButtonBitForNewSlot(0, 1, true) |
                                       ShiftButtonBitForNewSlot(2, 1, true) |
                                       ShiftButtonBitForNewSlot(0, 2, true) |
                                       ShiftButtonBitForNewSlot(3, 4, true);
      aControllerState.numButtons = 4;
      aControllerState.numAxes = 2;
      break;
    case VRControllerType::MSMR:
      aControllerState.buttonPressed =
          ShiftButtonBitForNewSlot(1, 0) | ShiftButtonBitForNewSlot(2, 1) |
          ShiftButtonBitForNewSlot(0, 2) | ShiftButtonBitForNewSlot(3, 3) |
          ShiftButtonBitForNewSlot(4, 4);
      aControllerState.buttonTouched = ShiftButtonBitForNewSlot(1, 0, true) |
                                       ShiftButtonBitForNewSlot(2, 1, true) |
                                       ShiftButtonBitForNewSlot(0, 2, true) |
                                       ShiftButtonBitForNewSlot(3, 3, true) |
                                       ShiftButtonBitForNewSlot(4, 4, true);
      aControllerState.numButtons = 5;
      aControllerState.numAxes = 4;
      break;
    case VRControllerType::HTCViveCosmos:
      aControllerState.buttonPressed =
          ShiftButtonBitForNewSlot(0, 0) | ShiftButtonBitForNewSlot(1, 1) |
          ShiftButtonBitForNewSlot(4, 3) | ShiftButtonBitForNewSlot(2, 4) |
          ShiftButtonBitForNewSlot(3, 5) | ShiftButtonBitForNewSlot(5, 6);
      aControllerState.buttonTouched = ShiftButtonBitForNewSlot(0, 0, true) |
                                       ShiftButtonBitForNewSlot(1, 1, true) |
                                       ShiftButtonBitForNewSlot(4, 3, true) |
                                       ShiftButtonBitForNewSlot(2, 4, true) |
                                       ShiftButtonBitForNewSlot(3, 5, true) |
                                       ShiftButtonBitForNewSlot(5, 6, true);
      aControllerState.axisValue[0] = aControllerState.axisValue[2];
      aControllerState.axisValue[1] = aControllerState.axisValue[3];
      aControllerState.numButtons = 6;
      aControllerState.numAxes = 2;
      break;
    case VRControllerType::HTCViveFocus:
      aControllerState.buttonPressed =
          ShiftButtonBitForNewSlot(0, 2) | ShiftButtonBitForNewSlot(1, 0);
      aControllerState.buttonTouched = ShiftButtonBitForNewSlot(0, 2, true) |
                                       ShiftButtonBitForNewSlot(1, 0, true);
      aControllerState.numButtons = 2;
      aControllerState.numAxes = 2;
      break;
    case VRControllerType::HTCViveFocusPlus: {
      aControllerState.buttonPressed = ShiftButtonBitForNewSlot(0, 2) |
                                       ShiftButtonBitForNewSlot(1, 0) |
                                       ShiftButtonBitForNewSlot(2, 1);
      aControllerState.buttonTouched = ShiftButtonBitForNewSlot(0, 2, true) |
                                       ShiftButtonBitForNewSlot(1, 0, true) |
                                       ShiftButtonBitForNewSlot(2, 1, true);
      aControllerState.numButtons = 3;
      aControllerState.numAxes = 2;

      static Matrix4x4 focusPlusTransform;
      Matrix4x4 originalMtx;
      if (focusPlusTransform.IsIdentity()) {
        focusPlusTransform.RotateX(-0.70f);
        focusPlusTransform.PostTranslate(0.0f, 0.0f, 0.01f);
        focusPlusTransform.Inverse();
      }
      gfx::Quaternion quat(aControllerState.pose.orientation[0],
                           aControllerState.pose.orientation[1],
                           aControllerState.pose.orientation[2],
                           aControllerState.pose.orientation[3]);
      originalMtx.SetRotationFromQuaternion(quat);
      originalMtx._41 = aControllerState.pose.position[0];
      originalMtx._42 = aControllerState.pose.position[1];
      originalMtx._43 = aControllerState.pose.position[2];
      originalMtx = focusPlusTransform * originalMtx;

      gfx::Point3D pos, scale;
      originalMtx.Decompose(pos, quat, scale);

      aControllerState.pose.position[0] = pos.x;
      aControllerState.pose.position[1] = pos.y;
      aControllerState.pose.position[2] = pos.z;

      aControllerState.pose.orientation[0] = quat.x;
      aControllerState.pose.orientation[1] = quat.y;
      aControllerState.pose.orientation[2] = quat.z;
      aControllerState.pose.orientation[3] = quat.w;
      break;
    }
    case VRControllerType::OculusGo: {
      aControllerState.buttonPressed =
          ShiftButtonBitForNewSlot(0, 2) | ShiftButtonBitForNewSlot(1, 0);
      aControllerState.buttonTouched = ShiftButtonBitForNewSlot(0, 2, true) |
                                       ShiftButtonBitForNewSlot(1, 0, true);
      aControllerState.numButtons = 2;
      aControllerState.numAxes = 2;

      static Matrix4x4 goTransform;
      Matrix4x4 originalMtx;

      if (goTransform.IsIdentity()) {
        goTransform.RotateX(-0.60f);
        goTransform.Inverse();
      }
      gfx::Quaternion quat(aControllerState.pose.orientation[0],
                           aControllerState.pose.orientation[1],
                           aControllerState.pose.orientation[2],
                           aControllerState.pose.orientation[3]);
      originalMtx.SetRotationFromQuaternion(quat);
      originalMtx._41 = aControllerState.pose.position[0];
      originalMtx._42 = aControllerState.pose.position[1];
      originalMtx._43 = aControllerState.pose.position[2];
      originalMtx = goTransform * originalMtx;

      gfx::Point3D pos, scale;
      originalMtx.Decompose(pos, quat, scale);

      aControllerState.pose.position[0] = pos.x;
      aControllerState.pose.position[1] = pos.y;
      aControllerState.pose.position[2] = pos.z;

      aControllerState.pose.orientation[0] = quat.x;
      aControllerState.pose.orientation[1] = quat.y;
      aControllerState.pose.orientation[2] = quat.z;
      aControllerState.pose.orientation[3] = quat.w;
      break;
    }
    case VRControllerType::OculusTouch:
      aControllerState.buttonPressed =
          ShiftButtonBitForNewSlot(0, 3) | ShiftButtonBitForNewSlot(1, 0) |
          ShiftButtonBitForNewSlot(2, 1) | ShiftButtonBitForNewSlot(3, 4) |
          ShiftButtonBitForNewSlot(4, 5) | ShiftButtonBitForNewSlot(5, 6);
      aControllerState.buttonTouched = ShiftButtonBitForNewSlot(0, 3, true) |
                                       ShiftButtonBitForNewSlot(1, 0, true) |
                                       ShiftButtonBitForNewSlot(2, 1, true) |
                                       ShiftButtonBitForNewSlot(3, 4, true) |
                                       ShiftButtonBitForNewSlot(4, 5, true) |
                                       ShiftButtonBitForNewSlot(5, 6, true);
      aControllerState.axisValue[0] = aControllerState.axisValue[2];
      aControllerState.axisValue[1] = aControllerState.axisValue[3];
      aControllerState.numButtons = 6;
      aControllerState.numAxes = 2;
      break;
    case VRControllerType::OculusTouch2:
    case VRControllerType::OculusTouch3: {
      aControllerState.buttonPressed =
          ShiftButtonBitForNewSlot(0, 3) | ShiftButtonBitForNewSlot(1, 0) |
          ShiftButtonBitForNewSlot(2, 1) | ShiftButtonBitForNewSlot(3, 4) |
          ShiftButtonBitForNewSlot(4, 5) | ShiftButtonBitForNewSlot(5, 6);
      aControllerState.buttonTouched = ShiftButtonBitForNewSlot(0, 3, true) |
                                       ShiftButtonBitForNewSlot(1, 0, true) |
                                       ShiftButtonBitForNewSlot(2, 1, true) |
                                       ShiftButtonBitForNewSlot(3, 4, true) |
                                       ShiftButtonBitForNewSlot(4, 5, true) |
                                       ShiftButtonBitForNewSlot(5, 6, true);
      aControllerState.axisValue[0] = aControllerState.axisValue[2];
      aControllerState.axisValue[1] = aControllerState.axisValue[3];
      aControllerState.numButtons = 6;
      aControllerState.numAxes = 2;

      static Matrix4x4 touchTransform;
      Matrix4x4 originalMtx;

      if (touchTransform.IsIdentity()) {
        touchTransform.RotateX(-0.77f);
        touchTransform.PostTranslate(0.0f, 0.0f, -0.025f);
        touchTransform.Inverse();
      }
      gfx::Quaternion quat(aControllerState.pose.orientation[0],
                           aControllerState.pose.orientation[1],
                           aControllerState.pose.orientation[2],
                           aControllerState.pose.orientation[3]);
      originalMtx.SetRotationFromQuaternion(quat);
      originalMtx._41 = aControllerState.pose.position[0];
      originalMtx._42 = aControllerState.pose.position[1];
      originalMtx._43 = aControllerState.pose.position[2];
      originalMtx = touchTransform * originalMtx;

      gfx::Point3D pos, scale;
      originalMtx.Decompose(pos, quat, scale);

      aControllerState.pose.position[0] = pos.x;
      aControllerState.pose.position[1] = pos.y;
      aControllerState.pose.position[2] = pos.z;

      aControllerState.pose.orientation[0] = quat.x;
      aControllerState.pose.orientation[1] = quat.y;
      aControllerState.pose.orientation[2] = quat.z;
      aControllerState.pose.orientation[3] = quat.w;
      break;
    }
    case VRControllerType::ValveIndex:
      aControllerState.buttonPressed =
          ShiftButtonBitForNewSlot(1, 0) | ShiftButtonBitForNewSlot(2, 1) |
          ShiftButtonBitForNewSlot(0, 2) | ShiftButtonBitForNewSlot(5, 3) |
          ShiftButtonBitForNewSlot(3, 4) | ShiftButtonBitForNewSlot(4, 5) |
          ShiftButtonBitForNewSlot(6, 6) | ShiftButtonBitForNewSlot(7, 7) |
          ShiftButtonBitForNewSlot(8, 8) | ShiftButtonBitForNewSlot(9, 9);
      aControllerState.buttonTouched = ShiftButtonBitForNewSlot(1, 0, true) |
                                       ShiftButtonBitForNewSlot(2, 1, true) |
                                       ShiftButtonBitForNewSlot(0, 2, true) |
                                       ShiftButtonBitForNewSlot(5, 3, true) |
                                       ShiftButtonBitForNewSlot(3, 4, true) |
                                       ShiftButtonBitForNewSlot(4, 5, true) |
                                       ShiftButtonBitForNewSlot(6, 6, true) |
                                       ShiftButtonBitForNewSlot(7, 7, true) |
                                       ShiftButtonBitForNewSlot(8, 8, true) |
                                       ShiftButtonBitForNewSlot(9, 9, true);
      aControllerState.numButtons = 10;
      aControllerState.numAxes = 4;
      break;
    case VRControllerType::PicoGaze:
      aControllerState.buttonPressed = ShiftButtonBitForNewSlot(0, 0);
      aControllerState.buttonTouched = ShiftButtonBitForNewSlot(0, 0, true);
      aControllerState.numButtons = 1;
      aControllerState.numAxes = 0;
      break;
    case VRControllerType::PicoG2:
      aControllerState.buttonPressed =
          ShiftButtonBitForNewSlot(0, 2) | ShiftButtonBitForNewSlot(1, 0);
      aControllerState.buttonTouched = ShiftButtonBitForNewSlot(0, 2, true) |
                                       ShiftButtonBitForNewSlot(1, 0, true);
      aControllerState.numButtons = 2;
      aControllerState.numAxes = 2;
      break;
    case VRControllerType::PicoNeo2:
      aControllerState.buttonPressed =
          ShiftButtonBitForNewSlot(0, 3) | ShiftButtonBitForNewSlot(1, 0) |
          ShiftButtonBitForNewSlot(2, 1) | ShiftButtonBitForNewSlot(3, 4) |
          ShiftButtonBitForNewSlot(4, 5) | ShiftButtonBitForNewSlot(5, 6);
      aControllerState.buttonTouched = ShiftButtonBitForNewSlot(0, 3, true) |
                                       ShiftButtonBitForNewSlot(1, 0, true) |
                                       ShiftButtonBitForNewSlot(2, 1, true) |
                                       ShiftButtonBitForNewSlot(3, 4, true) |
                                       ShiftButtonBitForNewSlot(4, 5, true) |
                                       ShiftButtonBitForNewSlot(5, 6, true);
      aControllerState.axisValue[0] = aControllerState.axisValue[2];
      aControllerState.axisValue[1] = aControllerState.axisValue[3];
      aControllerState.numButtons = 6;
      aControllerState.numAxes = 2;
      break;
    default:
      // Undefined controller types, we will keep its the same order.
      break;
  }
}

void VRDisplayClient::FireGamepadEvents() {
  RefPtr<dom::GamepadManager> gamepadManager(dom::GamepadManager::GetService());
  if (!gamepadManager) {
    return;
  }
  for (int stateIndex = 0; stateIndex < kVRControllerMaxCount; stateIndex++) {
    VRControllerState state = {}, lastState = {};
    memcpy(&state, &mDisplayInfo.mControllerState[stateIndex],
           sizeof(VRControllerState));
    memcpy(&lastState, &mLastEventControllerState[stateIndex],
           sizeof(VRControllerState));
    GamepadMappingForWebVR(state);
    GamepadMappingForWebVR(lastState);

    uint32_t gamepadHandleValue =
        mDisplayInfo.mDisplayID * kVRControllerMaxCount + stateIndex;

    GamepadHandle gamepadHandle{gamepadHandleValue, GamepadHandleKind::VR};

    bool bIsNew = false;

    // Send events to notify that controllers are removed
    if (state.controllerName[0] == '\0') {
      // Controller is not present
      if (lastState.controllerName[0] != '\0') {
        // Controller has been removed
        dom::GamepadRemoved info;
        dom::GamepadChangeEventBody body(info);
        dom::GamepadChangeEvent event(gamepadHandle, body);
        gamepadManager->Update(event);
      }
      // Do not process any further events for removed controllers
      continue;
    }

    // Send events to notify that new controllers are added
    RefPtr<dom::Gamepad> existing = gamepadManager->GetGamepad(gamepadHandle);
    // ControllerState in OpenVR action-based API gets delay to query btn and
    // axis count. So, we need to check if they are more than zero.
    if ((lastState.controllerName[0] == '\0' || !existing) &&
        (state.numButtons > 0 || state.numAxes > 0)) {
      dom::GamepadAdded info(NS_ConvertUTF8toUTF16(state.controllerName.data()),
                             dom::GamepadMappingType::_empty, state.hand,
                             state.numButtons, state.numAxes, state.numHaptics,
                             0, 0);
      dom::GamepadChangeEventBody body(info);
      dom::GamepadChangeEvent event(gamepadHandle, body);
      gamepadManager->Update(event);
      bIsNew = true;
    }

    // Send events for handedness changes
    if (state.hand != lastState.hand) {
      dom::GamepadHandInformation info(state.hand);
      dom::GamepadChangeEventBody body(info);
      dom::GamepadChangeEvent event(gamepadHandle, body);
      gamepadManager->Update(event);
    }

    // Send events for axis value changes
    for (uint32_t axisIndex = 0; axisIndex < state.numAxes; axisIndex++) {
      if (state.axisValue[axisIndex] != lastState.axisValue[axisIndex]) {
        dom::GamepadAxisInformation info(axisIndex, state.axisValue[axisIndex]);
        dom::GamepadChangeEventBody body(info);
        dom::GamepadChangeEvent event(gamepadHandle, body);
        gamepadManager->Update(event);
      }
    }

    // Send events for trigger, touch, and button value changes
    if (!bIsNew) {
      // When a new controller is added, we do not emit button events for
      // the initial state of the inputs.
      for (uint32_t buttonIndex = 0; buttonIndex < state.numButtons;
           buttonIndex++) {
        bool bPressed = (state.buttonPressed & (1ULL << buttonIndex)) != 0;
        bool bTouched = (state.buttonTouched & (1ULL << buttonIndex)) != 0;
        bool bLastPressed =
            (lastState.buttonPressed & (1ULL << buttonIndex)) != 0;
        bool bLastTouched =
            (lastState.buttonTouched & (1ULL << buttonIndex)) != 0;

        if (state.triggerValue[buttonIndex] !=
                lastState.triggerValue[buttonIndex] ||
            bPressed != bLastPressed || bTouched != bLastTouched) {
          dom::GamepadButtonInformation info(
              buttonIndex, state.triggerValue[buttonIndex], bPressed, bTouched);
          dom::GamepadChangeEventBody body(info);
          dom::GamepadChangeEvent event(gamepadHandle, body);
          gamepadManager->Update(event);
        }
      }
    }

    // Send events for pose changes
    // Note that VRPose is asserted to be a POD type so memcmp is safe
    if (state.flags != lastState.flags ||
        state.isPositionValid != lastState.isPositionValid ||
        state.isOrientationValid != lastState.isOrientationValid ||
        memcmp(&state.pose, &lastState.pose, sizeof(VRPose)) != 0) {
      // Convert pose to GamepadPoseState
      dom::GamepadPoseState poseState;
      poseState.Clear();
      poseState.flags = state.flags;

      // Orientation values
      poseState.isOrientationValid = state.isOrientationValid;
      poseState.orientation[0] = state.pose.orientation[0];
      poseState.orientation[1] = state.pose.orientation[1];
      poseState.orientation[2] = state.pose.orientation[2];
      poseState.orientation[3] = state.pose.orientation[3];
      poseState.angularVelocity[0] = state.pose.angularVelocity[0];
      poseState.angularVelocity[1] = state.pose.angularVelocity[1];
      poseState.angularVelocity[2] = state.pose.angularVelocity[2];
      poseState.angularAcceleration[0] = state.pose.angularAcceleration[0];
      poseState.angularAcceleration[1] = state.pose.angularAcceleration[1];
      poseState.angularAcceleration[2] = state.pose.angularAcceleration[2];

      // Position values
      poseState.isPositionValid = state.isPositionValid;
      poseState.position[0] = state.pose.position[0];
      poseState.position[1] = state.pose.position[1];
      poseState.position[2] = state.pose.position[2];
      poseState.linearVelocity[0] = state.pose.linearVelocity[0];
      poseState.linearVelocity[1] = state.pose.linearVelocity[1];
      poseState.linearVelocity[2] = state.pose.linearVelocity[2];
      poseState.linearAcceleration[0] = state.pose.linearAcceleration[0];
      poseState.linearAcceleration[1] = state.pose.linearAcceleration[1];
      poseState.linearAcceleration[2] = state.pose.linearAcceleration[2];

      // Send the event
      dom::GamepadPoseInformation info(poseState);
      dom::GamepadChangeEventBody body(info);
      dom::GamepadChangeEvent event(gamepadHandle, body);
      gamepadManager->Update(event);
    }
  }

  mLastEventControllerState = mDisplayInfo.mControllerState;
}

const VRHMDSensorState& VRDisplayClient::GetSensorState() const {
  return mDisplayInfo.GetSensorState();
}

bool VRDisplayClient::GetIsConnected() const {
  return mDisplayInfo.GetIsConnected();
}

bool VRDisplayClient::IsPresenting() {
  return mDisplayInfo.mPresentingGroups != 0;
}

void VRDisplayClient::NotifyDisconnected() {
  mDisplayInfo.mDisplayState.isConnected = false;
}

void VRDisplayClient::UpdateSubmitFrameResult(
    const VRSubmitFrameResultInfo& aResult) {
  mSubmitFrameResult = aResult;
}

void VRDisplayClient::GetSubmitFrameResult(VRSubmitFrameResultInfo& aResult) {
  aResult = mSubmitFrameResult;
}

void VRDisplayClient::StartVRNavigation() {
  /**
   * A VR-to-VR site navigation has started, notify VRManager
   * so we don't drop out of VR during the transition
   */
  VRManagerChild* vm = VRManagerChild::Get();
  vm->SendStartVRNavigation(mDisplayInfo.mDisplayID);
}

void VRDisplayClient::StopVRNavigation(const TimeDuration& aTimeout) {
  /**
   * A VR-to-VR site navigation has ended and the new site
   * has received a vrdisplayactivate event.
   * Don't actually consider the navigation transition over
   * until aTimeout has elapsed.
   * This may be called multiple times, in which case the timeout
   * should be reset to aTimeout.
   * When aTimeout is TimeDuration(0), we should consider the
   * transition immediately ended.
   */
  VRManagerChild* vm = VRManagerChild::Get();
  vm->SendStopVRNavigation(mDisplayInfo.mDisplayID, aTimeout);
}

bool VRDisplayClient::IsReferenceSpaceTypeSupported(
    dom::XRReferenceSpaceType aType) const {
  /**
   * https://immersive-web.github.io/webxr/#reference-space-is-supported
   *
   * We do not yet support local or local-floor for inline sessions.
   * This could be expanded if we later support WebXR for inline-ar
   * sessions on Firefox Fenix.
   *
   * We do not yet support unbounded reference spaces.
   */
  switch (aType) {
    case dom::XRReferenceSpaceType::Viewer:
      // Viewer is always supported, for both inline and immersive sessions
      return true;
    case dom::XRReferenceSpaceType::Local:
    case dom::XRReferenceSpaceType::Local_floor:
      // Local and Local_Floor are always supported for immersive sessions
      return bool(mDisplayInfo.GetCapabilities() &
                  (VRDisplayCapabilityFlags::Cap_ImmersiveVR |
                   VRDisplayCapabilityFlags::Cap_ImmersiveAR));
    case dom::XRReferenceSpaceType::Bounded_floor:
      return bool(mDisplayInfo.GetCapabilities() &
                  VRDisplayCapabilityFlags::Cap_StageParameters);
    default:
      NS_WARNING(
          "Unknown XRReferenceSpaceType passed to "
          "VRDisplayClient::IsReferenceSpaceTypeSupported");
      return false;
  }
}
