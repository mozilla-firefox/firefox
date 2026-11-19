/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_gamepad_GamepadPose_h
#define mozilla_dom_gamepad_GamepadPose_h

#include "mozilla/dom/Pose.h"
#include "nsCOMPtr.h"

namespace mozilla::dom {

class GamepadPose final : public nsWrapperCache {
 public:
  explicit GamepadPose(nsISupports* aParent);

  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(GamepadPose)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(GamepadPose)

  nsISupports* GetParentObject() const { return mParent; }
  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

  bool HasOrientation() const { return false; }
  bool HasPosition() const { return false; };
  void GetPosition(JSContext* aJSContext, JS::MutableHandle<JSObject*> aRetval,
                   ErrorResult& aRv);
  void GetLinearVelocity(JSContext* aJSContext,
                         JS::MutableHandle<JSObject*> aRetval,
                         ErrorResult& aRv);
  void GetLinearAcceleration(JSContext* aJSContext,
                             JS::MutableHandle<JSObject*> aRetval,
                             ErrorResult& aRv);
  void GetOrientation(JSContext* aJSContext,
                      JS::MutableHandle<JSObject*> aRetval, ErrorResult& aRv);
  void GetAngularVelocity(JSContext* aJSContext,
                          JS::MutableHandle<JSObject*> aRetval,
                          ErrorResult& aRv);
  void GetAngularAcceleration(JSContext* aJSContext,
                              JS::MutableHandle<JSObject*> aRetval,
                              ErrorResult& aRv);

 private:
  virtual ~GamepadPose() = default;

  nsCOMPtr<nsISupports> mParent;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_gamepad_GamepadPose_h
