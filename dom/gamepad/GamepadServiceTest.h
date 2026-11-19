/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_GamepadServiceTest_h_
#define mozilla_dom_GamepadServiceTest_h_

#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/GamepadBinding.h"
#include "mozilla/dom/GamepadHandle.h"
#include "mozilla/dom/TypedArray.h"

namespace mozilla::dom {

class GamepadChangeEvent;
class GamepadManager;
class GamepadTestChannelChild;
class Promise;

// Service for testing purposes
class GamepadServiceTest final : public DOMEventTargetHelper,
                                 public SupportsWeakPtr {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(GamepadServiceTest,
                                           DOMEventTargetHelper)

  GamepadMappingType NoMapping() const { return GamepadMappingType::_empty; }
  GamepadMappingType StandardMapping() const {
    return GamepadMappingType::Standard;
  }
  GamepadHand NoHand() const { return GamepadHand::_empty; }
  GamepadHand LeftHand() const { return GamepadHand::Left; }
  GamepadHand RightHand() const { return GamepadHand::Right; }

  // IPC receiver
  void ReplyGamepadHandle(uint32_t aPromiseId, const GamepadHandle& aHandle);

  // Methods from GamepadServiceTest.webidl
  already_AddRefed<Promise> AddGamepad(
      const nsAString& aID, GamepadMappingType aMapping, GamepadHand aHand,
      uint32_t aNumButtons, uint32_t aNumAxes, uint32_t aNumHaptics,
      uint32_t aNumLightIndicator, uint32_t aNumTouchEvents, ErrorResult& aRv);

  already_AddRefed<Promise> RemoveGamepad(uint32_t aHandleSlot,
                                          ErrorResult& aRv);

  already_AddRefed<Promise> NewButtonEvent(uint32_t aHandleSlot,
                                           uint32_t aButton, bool aPressed,
                                           bool aTouched, ErrorResult& aRv);

  already_AddRefed<Promise> NewButtonValueEvent(uint32_t aHandleSlot,
                                                uint32_t aButton, bool aPressed,
                                                bool aTouched, double aValue,
                                                ErrorResult& aRv);

  already_AddRefed<Promise> NewAxisMoveEvent(uint32_t aHandleSlot,
                                             uint32_t aAxis, double aValue,
                                             ErrorResult& aRv);

  already_AddRefed<Promise> NewPoseMove(
      uint32_t aHandleSlot, const Nullable<Float32Array>& aOrient,
      const Nullable<Float32Array>& aPos,
      const Nullable<Float32Array>& aAngVelocity,
      const Nullable<Float32Array>& aAngAcceleration,
      const Nullable<Float32Array>& aLinVelocity,
      const Nullable<Float32Array>& aLinAcceleration, ErrorResult& aRv);

  already_AddRefed<Promise> NewTouch(uint32_t aHandleSlot,
                                     uint32_t aTouchArrayIndex,
                                     uint32_t aTouchId, uint8_t aSurfaceId,
                                     const Float32Array& aPos,
                                     const Nullable<Float32Array>& aSurfDim,
                                     ErrorResult& aRv);

  void Shutdown();

  static already_AddRefed<GamepadServiceTest> CreateTestService(
      nsPIDOMWindowInner* aWindow);
  nsPIDOMWindowInner* GetParentObject() const { return mWindow; }
  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

 private:
  // Hold a reference to the gamepad service so we don't have to worry about
  // execution order in tests.
  RefPtr<GamepadManager> mService;
  nsCOMPtr<nsPIDOMWindowInner> mWindow;
  uint32_t mEventNumber;
  bool mShuttingDown;

  // IPDL Channel for us to send test events to GamepadPlatformService, it
  // will only be used in this singleton class and deleted during the IPDL
  // shutdown chain
  RefPtr<GamepadTestChannelChild> mChild;
  nsTArray<GamepadHandle> mGamepadHandles;

  nsRefPtrHashtable<nsUint32HashKey, dom::Promise> mPromiseList;

  explicit GamepadServiceTest(nsPIDOMWindowInner* aWindow);
  ~GamepadServiceTest();
  void InitPBackgroundActor();
  void DestroyPBackgroundActor();

  uint32_t AddGamepadHandle(GamepadHandle aHandle);
  void RemoveGamepadHandle(uint32_t aHandleSlot);
  GamepadHandle GetHandleInSlot(uint32_t aHandleSlot) const;
};

}  // namespace mozilla::dom

#endif
