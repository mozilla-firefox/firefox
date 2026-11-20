/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/GamepadPose.h"

#include "mozilla/dom/GamepadPoseBinding.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(GamepadPose, mParent)

GamepadPose::GamepadPose(nsISupports* aParent) : mParent(aParent) {}

/* virtual */
JSObject* GamepadPose::WrapObject(JSContext* aJSContext,
                                  JS::Handle<JSObject*> aGivenProto) {
  return GamepadPose_Binding::Wrap(aJSContext, this, aGivenProto);
}

void GamepadPose::GetPosition(JSContext* aJSContext,
                              JS::MutableHandle<JSObject*> aRetval,
                              ErrorResult& aRv) {
  aRetval.set(nullptr);
}

void GamepadPose::GetLinearVelocity(JSContext* aJSContext,
                                    JS::MutableHandle<JSObject*> aRetval,
                                    ErrorResult& aRv) {
  aRetval.set(nullptr);
}

void GamepadPose::GetLinearAcceleration(JSContext* aJSContext,
                                        JS::MutableHandle<JSObject*> aRetval,
                                        ErrorResult& aRv) {
  aRetval.set(nullptr);
}

void GamepadPose::GetOrientation(JSContext* aJSContext,
                                 JS::MutableHandle<JSObject*> aRetval,
                                 ErrorResult& aRv) {
  aRetval.set(nullptr);
}

void GamepadPose::GetAngularVelocity(JSContext* aJSContext,
                                     JS::MutableHandle<JSObject*> aRetval,
                                     ErrorResult& aRv) {
  aRetval.set(nullptr);
}

void GamepadPose::GetAngularAcceleration(JSContext* aJSContext,
                                         JS::MutableHandle<JSObject*> aRetval,
                                         ErrorResult& aRv) {
  aRetval.set(nullptr);
}

}  // namespace mozilla::dom
