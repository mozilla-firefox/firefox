/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/XRInputSource.h"

#include "VRDisplayClient.h"
#include "XRInputSpace.h"
#include "XRNativeOriginTracker.h"
#include "XRNativeOriginViewer.h"
#include "mozilla/dom/Gamepad.h"
#include "mozilla/dom/GamepadManager.h"
#include "mozilla/dom/XRInputSourceEvent.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(XRInputSource, mParent, mTargetRaySpace,
                                      mGripSpace, mGamepad)

// Follow the controller profile ids from
// https://github.com/immersive-web/webxr-input-profiles.
nsTArray<nsString> GetInputSourceProfile(gfx::VRControllerType aType) {
  nsTArray<nsString> profile;
  nsString id;

  switch (aType) {
    case gfx::VRControllerType::HTCVive:
      id.AssignLiteral("htc-vive");
      profile.AppendElement(id);
      id.AssignLiteral("generic-trigger-squeeze-touchpad");
      profile.AppendElement(id);
      break;
    case gfx::VRControllerType::HTCViveCosmos:
      id.AssignLiteral("htc-vive-cosmos");
      profile.AppendElement(id);
      id.AssignLiteral("generic-trigger-squeeze-thumbstick");
      profile.AppendElement(id);
      break;
    case gfx::VRControllerType::HTCViveFocus:
      id.AssignLiteral("htc-vive-focus");
      profile.AppendElement(id);
      id.AssignLiteral("generic-trigger-touchpad");
      profile.AppendElement(id);
      break;
    case gfx::VRControllerType::HTCViveFocusPlus:
      id.AssignLiteral("htc-vive-focus-plus");
      profile.AppendElement(id);
      id.AssignLiteral("generic-trigger-squeeze-touchpad");
      profile.AppendElement(id);
      break;
    case gfx::VRControllerType::MSMR:
      id.AssignLiteral("microsoft-mixed-reality");
      profile.AppendElement(id);
      id.AssignLiteral("generic-trigger-squeeze-touchpad-thumbstick");
      profile.AppendElement(id);
      break;
    case gfx::VRControllerType::ValveIndex:
      id.AssignLiteral("valve-index");
      profile.AppendElement(id);
      id.AssignLiteral("generic-trigger-squeeze-touchpad-thumbstick");
      profile.AppendElement(id);
      break;
    case gfx::VRControllerType::OculusGo:
      id.AssignLiteral("oculus-go");
      profile.AppendElement(id);
      id.AssignLiteral("generic-trigger-touchpad");
      profile.AppendElement(id);
      break;
    case gfx::VRControllerType::OculusTouch:
      id.AssignLiteral("oculus-touch");
      profile.AppendElement(id);
      id.AssignLiteral("generic-trigger-squeeze-thumbstick");
      profile.AppendElement(id);
      break;
    case gfx::VRControllerType::OculusTouch2:
      id.AssignLiteral("oculus-touch-v2");
      profile.AppendElement(id);
      id.AssignLiteral("oculus-touch");
      profile.AppendElement(id);
      id.AssignLiteral("generic-trigger-squeeze-thumbstick");
      profile.AppendElement(id);
      break;
    case gfx::VRControllerType::OculusTouch3:
      id.AssignLiteral("oculus-touch-v3");
      profile.AppendElement(id);
      id.AssignLiteral("oculus-touch-v2");
      profile.AppendElement(id);
      id.AssignLiteral("oculus-touch");
      profile.AppendElement(id);
      id.AssignLiteral("generic-trigger-squeeze-thumbstick");
      profile.AppendElement(id);
      break;
    case gfx::VRControllerType::PicoGaze:
      id.AssignLiteral("pico-gaze");
      profile.AppendElement(id);
      id.AssignLiteral("generic-button");
      profile.AppendElement(id);
      break;
    case gfx::VRControllerType::PicoG2:
      id.AssignLiteral("pico-g2");
      profile.AppendElement(id);
      id.AssignLiteral("generic-trigger-touchpad");
      profile.AppendElement(id);
      break;
    case gfx::VRControllerType::PicoNeo2:
      id.AssignLiteral("pico-neo2");
      profile.AppendElement(id);
      id.AssignLiteral("generic-trigger-squeeze-thumbstick");
      profile.AppendElement(id);
      break;
    default:
      NS_WARNING("Unsupported XR input source profile.");
      break;
  }
  return profile;
}

XRInputSource::XRInputSource(nsISupports* aParent)
    : mParent(aParent),
      mGamepad(nullptr),
      mIndex(-1),
      mSelectAction(ActionState::ActionState_Released),
      mSqueezeAction(ActionState::ActionState_Released) {}

XRInputSource::~XRInputSource() {
  mTargetRaySpace = nullptr;
  mGripSpace = nullptr;
  mGamepad = nullptr;
}

JSObject* XRInputSource::WrapObject(JSContext* aCx,
                                    JS::Handle<JSObject*> aGivenProto) {
  return XRInputSource_Binding::Wrap(aCx, this, aGivenProto);
}

XRHandedness XRInputSource::Handedness() { return mHandedness; }

XRTargetRayMode XRInputSource::TargetRayMode() { return mTargetRayMode; }

XRSpace* XRInputSource::TargetRaySpace() { return mTargetRaySpace; }

XRSpace* XRInputSource::GetGripSpace() { return mGripSpace; }

void XRInputSource::GetProfiles(nsTArray<nsString>& aResult) {
  aResult = mProfiles.Clone();
}

Gamepad* XRInputSource::GetGamepad() { return mGamepad; }

void XRInputSource::Setup(XRSession* aSession, uint32_t aIndex) {
  MOZ_ASSERT(aSession);
  gfx::VRDisplayClient* displayClient = aSession->GetDisplayClient();
  if (!displayClient) {
    MOZ_ASSERT(displayClient);
    return;
  }
  const gfx::VRDisplayInfo& displayInfo = displayClient->GetDisplayInfo();
  const gfx::VRControllerState& controllerState =
      displayInfo.mControllerState[aIndex];
  MOZ_ASSERT(controllerState.controllerName[0] != '\0');

  mProfiles = GetInputSourceProfile(controllerState.type);
  mHandedness = XRHandedness::None;
  switch (controllerState.hand) {
    case GamepadHand::_empty:
      mHandedness = XRHandedness::None;
      break;
    case GamepadHand::Left:
      mHandedness = XRHandedness::Left;
      break;
    case GamepadHand::Right:
      mHandedness = XRHandedness::Right;
      break;
    default:
      MOZ_ASSERT(false && "Unknown GamepadHand type.");
      break;
  }

  RefPtr<XRNativeOrigin> nativeOriginTargetRay = nullptr;
  mTargetRayMode = XRTargetRayMode::Tracked_pointer;
  switch (controllerState.targetRayMode) {
    case gfx::TargetRayMode::Gaze:
      mTargetRayMode = XRTargetRayMode::Gaze;
      nativeOriginTargetRay = new XRNativeOriginViewer(displayClient);
      break;
    case gfx::TargetRayMode::TrackedPointer:
      mTargetRayMode = XRTargetRayMode::Tracked_pointer;
      // We use weak pointers of poses in XRNativeOriginTracker to sync their
      // data internally.
      nativeOriginTargetRay =
          new XRNativeOriginTracker(&controllerState.targetRayPose);
      break;
    case gfx::TargetRayMode::Screen:
      mTargetRayMode = XRTargetRayMode::Screen;
      break;
    default:
      MOZ_ASSERT(false && "Undefined TargetRayMode type.");
      break;
  }

  mTargetRaySpace = new XRInputSpace(aSession->GetParentObject(), aSession,
                                     nativeOriginTargetRay, aIndex);

  const uint32_t gamepadHandleValue =
      displayInfo.mDisplayID * gfx::kVRControllerMaxCount + aIndex;

  const GamepadHandle gamepadHandle{gamepadHandleValue, GamepadHandleKind::VR};

  mGamepad = new Gamepad(mParent, NS_ConvertASCIItoUTF16(""), -1, gamepadHandle,
                         GamepadMappingType::Xr_standard, controllerState.hand,
                         controllerState.numButtons, controllerState.numAxes,
                         controllerState.numHaptics, 0, 0);
  mIndex = aIndex;

  if (!mGripSpace) {
    CreateGripSpace(aSession, controllerState);
  }
}

void XRInputSource::SetGamepadIsConnected(bool aConnected,
                                          XRSession* aSession) {
  mGamepad->SetConnected(aConnected);
  MOZ_ASSERT(aSession);

  if (!aConnected) {
    if (mSelectAction != ActionState::ActionState_Released) {
      DispatchEvent(u"selectend"_ns, aSession);
      mSelectAction = ActionState::ActionState_Released;
    }
    if (mSqueezeAction != ActionState::ActionState_Released) {
      DispatchEvent(u"squeezeend"_ns, aSession);
      mSqueezeAction = ActionState::ActionState_Released;
    }
  }
}

void XRInputSource::Update(XRSession* aSession) {
  MOZ_ASSERT(aSession && mIndex >= 0 && mGamepad);

  gfx::VRDisplayClient* displayClient = aSession->GetDisplayClient();
  if (!displayClient) {
    MOZ_ASSERT(displayClient);
    return;
  }
  const gfx::VRDisplayInfo& displayInfo = displayClient->GetDisplayInfo();
  const gfx::VRControllerState& controllerState =
      displayInfo.mControllerState[mIndex];
  MOZ_ASSERT(controllerState.controllerName[0] != '\0');

  // OculusVR and OpenVR controllers need to wait until
  // update functions to assign GamepadCapabilityFlags::Cap_GripSpacePosition
  // flag.
  if (!mGripSpace) {
    CreateGripSpace(aSession, controllerState);
  }

  // Update button values.
  nsTArray<RefPtr<GamepadButton>> buttons;
  mGamepad->GetButtons(buttons);
  for (uint32_t i = 0; i < buttons.Length(); ++i) {
    const bool pressed = controllerState.buttonPressed & (1ULL << i);
    const bool touched = controllerState.buttonTouched & (1ULL << i);

    if (buttons[i]->Pressed() != pressed || buttons[i]->Touched() != touched ||
        buttons[i]->Value() != controllerState.triggerValue[i]) {
      mGamepad->SetButton(i, pressed, touched, controllerState.triggerValue[i]);
    }
  }
  // Update axis values.
  nsTArray<double> axes;
  mGamepad->GetAxes(axes);
  for (uint32_t i = 0; i < axes.Length(); ++i) {
    if (axes[i] != controllerState.axisValue[i]) {
      mGamepad->SetAxis(i, controllerState.axisValue[i]);
    }
  }

  // We define 0.85f and 0.15f based on our current finding
  // for better experience, we can adjust these values if we need.
  const float completeThreshold = 0.90f;
  const float startThreshold = 0.85f;
  const float endThreshold = 0.15f;
  const uint32_t selectIndex = 0;
  const uint32_t squeezeIndex = 1;

  // Checking selectstart, select, selectend
  if (buttons.Length() > selectIndex) {
    if (controllerState.selectActionStartFrameId >
        controllerState.selectActionStopFrameId) {
      if (mSelectAction == ActionState::ActionState_Released &&
          controllerState.triggerValue[selectIndex] > endThreshold) {
        DispatchEvent(u"selectstart"_ns, aSession);
        mSelectAction = ActionState::ActionState_Pressing;
      } else if (mSelectAction == ActionState::ActionState_Pressing &&
                 controllerState.triggerValue[selectIndex] >
                     completeThreshold) {
        mSelectAction = ActionState::ActionState_Pressed;
      } else if (mSelectAction == ActionState::ActionState_Pressed &&
                 controllerState.triggerValue[selectIndex] < startThreshold) {
        DispatchEvent(u"select"_ns, aSession);
        mSelectAction = ActionState::ActionState_Releasing;
      } else if (mSelectAction <= ActionState::ActionState_Releasing &&
                 controllerState.triggerValue[selectIndex] < endThreshold) {
        // For a select btn which only has pressed and unpressed status.
        if (mSelectAction == ActionState::ActionState_Pressed) {
          DispatchEvent(u"select"_ns, aSession);
        }
        DispatchEvent(u"selectend"_ns, aSession);
        mSelectAction = ActionState::ActionState_Released;
      }
    } else if (mSelectAction <= ActionState::ActionState_Releasing) {
      // For a select btn which only has pressed and unpressed status.
      if (mSelectAction == ActionState::ActionState_Pressed) {
        DispatchEvent(u"select"_ns, aSession);
      }
      DispatchEvent(u"selectend"_ns, aSession);
      mSelectAction = ActionState::ActionState_Released;
    }
  }

  // Checking squeezestart, squeeze, squeezeend
  if (buttons.Length() > squeezeIndex) {
    if (controllerState.squeezeActionStartFrameId >
        controllerState.squeezeActionStopFrameId) {
      if (mSqueezeAction == ActionState::ActionState_Released &&
          controllerState.triggerValue[squeezeIndex] > endThreshold) {
        DispatchEvent(u"squeezestart"_ns, aSession);
        mSqueezeAction = ActionState::ActionState_Pressing;
      } else if (mSqueezeAction == ActionState::ActionState_Pressing &&
                 controllerState.triggerValue[squeezeIndex] >
                     completeThreshold) {
        mSqueezeAction = ActionState::ActionState_Pressed;
      } else if (mSqueezeAction == ActionState::ActionState_Pressed &&
                 controllerState.triggerValue[squeezeIndex] < startThreshold) {
        DispatchEvent(u"squeeze"_ns, aSession);
        mSqueezeAction = ActionState::ActionState_Releasing;
      } else if (mSqueezeAction <= ActionState::ActionState_Releasing &&
                 controllerState.triggerValue[squeezeIndex] < endThreshold) {
        // For a squeeze btn which only has pressed and unpressed status.
        if (mSqueezeAction == ActionState::ActionState_Pressed) {
          DispatchEvent(u"squeeze"_ns, aSession);
        }
        DispatchEvent(u"squeezeend"_ns, aSession);
        mSqueezeAction = ActionState::ActionState_Released;
      }
    } else if (mSqueezeAction <= ActionState::ActionState_Releasing) {
      // For a squeeze btn which only has pressed and unpressed status.
      if (mSqueezeAction == ActionState::ActionState_Pressed) {
        DispatchEvent(u"squeeze"_ns, aSession);
      }
      DispatchEvent(u"squeezeend"_ns, aSession);
      mSqueezeAction = ActionState::ActionState_Released;
    }
  }
}

int32_t XRInputSource::GetIndex() { return mIndex; }

void XRInputSource::DispatchEvent(const nsAString& aEvent,
                                  XRSession* aSession) {
  if (!GetParentObject() || !aSession) {
    return;
  }
  // Create a XRFrame for its callbacks
  RefPtr<XRFrame> frame = new XRFrame(GetParentObject(), aSession);
  frame->StartInputSourceEvent();

  XRInputSourceEventInit init;
  init.mBubbles = false;
  init.mCancelable = false;
  init.mFrame = frame;
  init.mInputSource = this;

  RefPtr<XRInputSourceEvent> event =
      XRInputSourceEvent::Constructor(aSession, aEvent, init);

  event->SetTrusted(true);
  aSession->DispatchEvent(*event);
  frame->EndInputSourceEvent();
}

void XRInputSource::CreateGripSpace(
    XRSession* aSession, const gfx::VRControllerState& controllerState) {
  MOZ_ASSERT(!mGripSpace);
  MOZ_ASSERT(aSession && mIndex >= 0 && mGamepad);
  if (mTargetRayMode == XRTargetRayMode::Tracked_pointer &&
      controllerState.flags & GamepadCapabilityFlags::Cap_GripSpacePosition) {
    RefPtr<XRNativeOrigin> nativeOriginGrip = nullptr;
    nativeOriginGrip = new XRNativeOriginTracker(&controllerState.pose);
    mGripSpace = new XRInputSpace(aSession->GetParentObject(), aSession,
                                  nativeOriginGrip, mIndex);
  } else {
    mGripSpace = nullptr;
  }
}

}  // namespace mozilla::dom
