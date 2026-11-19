/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/GamepadPose.h"

#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/dom/GamepadPoseBinding.h"
#include "nsWrapperCache.h"

namespace mozilla::dom {

GamepadPose::GamepadPose(nsISupports* aParent, const GamepadPoseState& aState)
    : Pose(aParent), mPoseState(aState) {
  mozilla::HoldJSObjects(this);
}

GamepadPose::GamepadPose(nsISupports* aParent) : Pose(aParent) {
  mozilla::HoldJSObjects(this);
  mPoseState.Clear();
}

GamepadPose::~GamepadPose() { mozilla::DropJSObjects(this); }

/* virtual */
JSObject* GamepadPose::WrapObject(JSContext* aJSContext,
                                  JS::Handle<JSObject*> aGivenProto) {
  return GamepadPose_Binding::Wrap(aJSContext, this, aGivenProto);
}

bool GamepadPose::HasOrientation() const {
  return bool(mPoseState.flags & GamepadCapabilityFlags::Cap_Orientation);
}

bool GamepadPose::HasPosition() const {
  return bool(mPoseState.flags & GamepadCapabilityFlags::Cap_Position) ||
         bool(mPoseState.flags & GamepadCapabilityFlags::Cap_PositionEmulated);
}

void GamepadPose::GetPosition(JSContext* aJSContext,
                              JS::MutableHandle<JSObject*> aRetval,
                              ErrorResult& aRv) {
  const bool valid =
      mPoseState.isPositionValid &&
      (bool(mPoseState.flags & GamepadCapabilityFlags::Cap_Position) ||
       bool(mPoseState.flags & GamepadCapabilityFlags::Cap_PositionEmulated));
  SetFloat32Array(aJSContext, this, aRetval, mPosition,
                  valid ? mPoseState.position : nullptr, 3, aRv);
}

void GamepadPose::GetLinearVelocity(JSContext* aJSContext,
                                    JS::MutableHandle<JSObject*> aRetval,
                                    ErrorResult& aRv) {
  const bool valid =
      mPoseState.isPositionValid &&
      (bool(mPoseState.flags & GamepadCapabilityFlags::Cap_Position) ||
       bool(mPoseState.flags & GamepadCapabilityFlags::Cap_PositionEmulated));
  SetFloat32Array(aJSContext, this, aRetval, mLinearVelocity,
                  valid ? mPoseState.linearVelocity : nullptr, 3, aRv);
}

void GamepadPose::GetLinearAcceleration(JSContext* aJSContext,
                                        JS::MutableHandle<JSObject*> aRetval,
                                        ErrorResult& aRv) {
  const bool valid =
      mPoseState.isPositionValid &&
      bool(mPoseState.flags & GamepadCapabilityFlags::Cap_LinearAcceleration);
  SetFloat32Array(aJSContext, this, aRetval, mLinearAcceleration,
                  valid ? mPoseState.linearAcceleration : nullptr, 3, aRv);
}

void GamepadPose::GetOrientation(JSContext* aJSContext,
                                 JS::MutableHandle<JSObject*> aRetval,
                                 ErrorResult& aRv) {
  const bool valid =
      mPoseState.isOrientationValid &&
      bool(mPoseState.flags & GamepadCapabilityFlags::Cap_Orientation);
  SetFloat32Array(aJSContext, this, aRetval, mOrientation,
                  valid ? mPoseState.orientation : nullptr, 4, aRv);
}

void GamepadPose::GetAngularVelocity(JSContext* aJSContext,
                                     JS::MutableHandle<JSObject*> aRetval,
                                     ErrorResult& aRv) {
  const bool valid =
      mPoseState.isOrientationValid &&
      bool(mPoseState.flags & GamepadCapabilityFlags::Cap_Orientation);
  SetFloat32Array(aJSContext, this, aRetval, mAngularVelocity,
                  valid ? mPoseState.angularVelocity : nullptr, 3, aRv);
}

void GamepadPose::GetAngularAcceleration(JSContext* aJSContext,
                                         JS::MutableHandle<JSObject*> aRetval,
                                         ErrorResult& aRv) {
  const bool valid =
      mPoseState.isOrientationValid &&
      bool(mPoseState.flags & GamepadCapabilityFlags::Cap_AngularAcceleration);
  SetFloat32Array(aJSContext, this, aRetval, mAngularAcceleration,
                  valid ? mPoseState.angularAcceleration : nullptr, 3, aRv);
}

void GamepadPose::SetPoseState(const GamepadPoseState& aPose) {
  mPoseState = aPose;
}

const GamepadPoseState& GamepadPose::GetPoseState() { return mPoseState; }

}  // namespace mozilla::dom
