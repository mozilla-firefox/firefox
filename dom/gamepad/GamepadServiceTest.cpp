/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GamepadServiceTest.h"

#include "mozilla/ErrorResult.h"
#include "mozilla/dom/GamepadManager.h"
#include "mozilla/dom/GamepadPlatformService.h"
#include "mozilla/dom/GamepadServiceTestBinding.h"
#include "mozilla/dom/GamepadTestChannelChild.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"

namespace mozilla::dom {

/*
 * Implementation of the test service. This is just to provide a simple binding
 * of the GamepadService to JavaScript via WebIDL so that we can write
 * Mochitests that add and remove fake gamepads, avoiding the platform-specific
 * backends.
 */

constexpr uint32_t kMaxButtons = 20;
constexpr uint32_t kMaxAxes = 10;
constexpr uint32_t kMaxHaptics = 2;
constexpr uint32_t kMaxLightIndicator = 2;
constexpr uint32_t kMaxTouchEvents = 4;

NS_IMPL_CYCLE_COLLECTION_WEAK_PTR_INHERITED(GamepadServiceTest,
                                            DOMEventTargetHelper, mWindow)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(GamepadServiceTest)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(GamepadServiceTest, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(GamepadServiceTest, DOMEventTargetHelper)

// static
already_AddRefed<GamepadServiceTest> GamepadServiceTest::CreateTestService(
    nsPIDOMWindowInner* aWindow) {
  MOZ_ASSERT(aWindow);
  RefPtr<GamepadServiceTest> service = new GamepadServiceTest(aWindow);
  service->InitPBackgroundActor();
  return service.forget();
}

void GamepadServiceTest::Shutdown() {
  MOZ_ASSERT(!mShuttingDown);
  mShuttingDown = true;
  DestroyPBackgroundActor();
  mWindow = nullptr;
}

GamepadServiceTest::GamepadServiceTest(nsPIDOMWindowInner* aWindow)
    : mService(GamepadManager::GetService()),
      mWindow(aWindow),
      mEventNumber(0),
      mShuttingDown(false),
      mChild(nullptr) {}

GamepadServiceTest::~GamepadServiceTest() {
  MOZ_ASSERT(mPromiseList.IsEmpty());
}

void GamepadServiceTest::InitPBackgroundActor() {
  MOZ_ASSERT(!mChild);

  ::mozilla::ipc::PBackgroundChild* actor =
      ::mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!actor)) {
    MOZ_CRASH("Failed to create a PBackgroundChild actor!");
  }

  mChild = GamepadTestChannelChild::Create(this);
  PGamepadTestChannelChild* initedChild =
      actor->SendPGamepadTestChannelConstructor(mChild.get());
  if (NS_WARN_IF(!initedChild)) {
    MOZ_CRASH("Failed to create a PBackgroundChild actor!");
  }
}

void GamepadServiceTest::ReplyGamepadHandle(uint32_t aPromiseId,
                                            const GamepadHandle& aHandle) {
  uint32_t handleSlot = AddGamepadHandle(aHandle);

  RefPtr<Promise> p;
  if (!mPromiseList.Get(aPromiseId, getter_AddRefs(p))) {
    MOZ_CRASH("We should always have a promise.");
  }

  p->MaybeResolve(handleSlot);
  mPromiseList.Remove(aPromiseId);
}

void GamepadServiceTest::DestroyPBackgroundActor() {
  MOZ_ASSERT(mChild);
  PGamepadTestChannelChild::Send__delete__(mChild);
  mChild = nullptr;
}

already_AddRefed<Promise> GamepadServiceTest::AddGamepad(
    const nsAString& aID, GamepadMappingType aMapping, GamepadHand aHand,
    uint32_t aNumButtons, uint32_t aNumAxes, uint32_t aNumHaptics,
    uint32_t aNumLightIndicator, uint32_t aNumTouchEvents, ErrorResult& aRv) {
  if (aNumButtons > kMaxButtons || aNumAxes > kMaxAxes ||
      aNumHaptics > kMaxHaptics || aNumLightIndicator > kMaxLightIndicator ||
      aNumTouchEvents > kMaxTouchEvents) {
    aRv.ThrowNotSupportedError("exceeded maximum hardware dimensions");
    return nullptr;
  }

  if (mShuttingDown) {
    aRv.ThrowInvalidStateError("Shutting down");
    return nullptr;
  }

  // The values here are ignored, the value just can't be zero to avoid an
  // assertion
  GamepadHandle gamepadHandle{1, GamepadHandleKind::GamepadPlatformManager};

  GamepadAdded a(nsString(aID), aMapping, aHand, aNumButtons, aNumAxes,
                 aNumHaptics, aNumLightIndicator, aNumTouchEvents);
  GamepadChangeEventBody body(a);
  GamepadChangeEvent e(gamepadHandle, body);

  RefPtr<Promise> p = Promise::Create(mWindow->AsGlobal(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  uint32_t id = ++mEventNumber;

  MOZ_ASSERT(!mPromiseList.Contains(id));
  mPromiseList.InsertOrUpdate(id, RefPtr{p});

  mChild->SendGamepadTestEvent(id, e);

  return p.forget();
}

already_AddRefed<Promise> GamepadServiceTest::RemoveGamepad(
    uint32_t aHandleSlot, ErrorResult& aRv) {
  if (mShuttingDown) {
    aRv.ThrowInvalidStateError("Shutting down");
    return nullptr;
  }

  GamepadHandle gamepadHandle = GetHandleInSlot(aHandleSlot);

  GamepadRemoved a;
  GamepadChangeEventBody body(a);
  GamepadChangeEvent e(gamepadHandle, body);

  uint32_t id = ++mEventNumber;

  RefPtr<Promise> p = Promise::Create(mWindow->AsGlobal(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  MOZ_ASSERT(!mPromiseList.Contains(id));
  mPromiseList.InsertOrUpdate(id, RefPtr{p});

  mChild->SendGamepadTestEvent(id, e);
  return p.forget();
}

already_AddRefed<Promise> GamepadServiceTest::NewButtonEvent(
    uint32_t aHandleSlot, uint32_t aButton, bool aPressed, bool aTouched,
    ErrorResult& aRv) {
  if (mShuttingDown) {
    aRv.ThrowInvalidStateError("Shutting down");
    return nullptr;
  }

  GamepadHandle gamepadHandle = GetHandleInSlot(aHandleSlot);

  GamepadButtonInformation a(aButton, aPressed ? 1.0 : 0, aPressed, aTouched);
  GamepadChangeEventBody body(a);
  GamepadChangeEvent e(gamepadHandle, body);

  uint32_t id = ++mEventNumber;
  RefPtr<Promise> p = Promise::Create(mWindow->AsGlobal(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  MOZ_ASSERT(!mPromiseList.Contains(id));
  mPromiseList.InsertOrUpdate(id, RefPtr{p});
  mChild->SendGamepadTestEvent(id, e);
  return p.forget();
}

already_AddRefed<Promise> GamepadServiceTest::NewButtonValueEvent(
    uint32_t aHandleSlot, uint32_t aButton, bool aPressed, bool aTouched,
    double aValue, ErrorResult& aRv) {
  if (mShuttingDown) {
    aRv.ThrowInvalidStateError("Shutting down");
    return nullptr;
  }

  GamepadHandle gamepadHandle = GetHandleInSlot(aHandleSlot);

  GamepadButtonInformation a(aButton, aValue, aPressed, aTouched);
  GamepadChangeEventBody body(a);
  GamepadChangeEvent e(gamepadHandle, body);

  uint32_t id = ++mEventNumber;
  RefPtr<Promise> p = Promise::Create(mWindow->AsGlobal(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  MOZ_ASSERT(!mPromiseList.Contains(id));
  mPromiseList.InsertOrUpdate(id, RefPtr{p});
  mChild->SendGamepadTestEvent(id, e);
  return p.forget();
}

already_AddRefed<Promise> GamepadServiceTest::NewAxisMoveEvent(
    uint32_t aHandleSlot, uint32_t aAxis, double aValue, ErrorResult& aRv) {
  if (mShuttingDown) {
    aRv.ThrowInvalidStateError("Shutting down");
    return nullptr;
  }

  GamepadHandle gamepadHandle = GetHandleInSlot(aHandleSlot);

  GamepadAxisInformation a(aAxis, aValue);
  GamepadChangeEventBody body(a);
  GamepadChangeEvent e(gamepadHandle, body);

  uint32_t id = ++mEventNumber;
  RefPtr<Promise> p = Promise::Create(mWindow->AsGlobal(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  MOZ_ASSERT(!mPromiseList.Contains(id));
  mPromiseList.InsertOrUpdate(id, RefPtr{p});
  mChild->SendGamepadTestEvent(id, e);
  return p.forget();
}

already_AddRefed<Promise> GamepadServiceTest::NewPoseMove(
    uint32_t aHandleSlot, const Nullable<Float32Array>& aOrient,
    const Nullable<Float32Array>& aPos,
    const Nullable<Float32Array>& aAngVelocity,
    const Nullable<Float32Array>& aAngAcceleration,
    const Nullable<Float32Array>& aLinVelocity,
    const Nullable<Float32Array>& aLinAcceleration, ErrorResult& aRv) {
  if (mShuttingDown) {
    aRv.ThrowInvalidStateError("Shutting down");
    return nullptr;
  }

  GamepadHandle gamepadHandle = GetHandleInSlot(aHandleSlot);

  GamepadPoseState poseState;
  poseState.flags = GamepadCapabilityFlags::Cap_Orientation |
                    GamepadCapabilityFlags::Cap_Position |
                    GamepadCapabilityFlags::Cap_AngularAcceleration |
                    GamepadCapabilityFlags::Cap_LinearAcceleration;
  if (!aOrient.IsNull()) {
    DebugOnly<bool> ok = aOrient.Value().CopyDataTo(poseState.orientation);
    MOZ_ASSERT(ok,
               "aOrient.Value().Length() != std::size(poseState.orientation)");
    poseState.isOrientationValid = true;
  }
  if (!aPos.IsNull()) {
    DebugOnly<bool> ok = aPos.Value().CopyDataTo(poseState.position);
    MOZ_ASSERT(ok, "aPos.Value().Length() != std::size(poseState.position)");
    poseState.isPositionValid = true;
  }
  if (!aAngVelocity.IsNull()) {
    DebugOnly<bool> ok =
        aAngVelocity.Value().CopyDataTo(poseState.angularVelocity);
    MOZ_ASSERT(ok,
               "aAngVelocity.Value().Length() != "
               "std::size(poseState.angularVelocity)");
  }
  if (!aAngAcceleration.IsNull()) {
    DebugOnly<bool> ok =
        aAngAcceleration.Value().CopyDataTo(poseState.angularAcceleration);
    MOZ_ASSERT(ok,
               "aAngAcceleration.Value().Length() != "
               "std::size(poseState.angularAcceleration)");
  }
  if (!aLinVelocity.IsNull()) {
    DebugOnly<bool> ok =
        aLinVelocity.Value().CopyDataTo(poseState.linearVelocity);
    MOZ_ASSERT(ok,
               "aLinVelocity.Value().Length() != "
               "std::size(poseState.linearVelocity)");
  }
  if (!aLinAcceleration.IsNull()) {
    DebugOnly<bool> ok =
        aLinAcceleration.Value().CopyDataTo(poseState.linearAcceleration);
    MOZ_ASSERT(ok,
               "aLinAcceleration.Value().Length() != "
               "std::size(poseState.linearAcceleration)");
  }

  GamepadPoseInformation a(poseState);
  GamepadChangeEventBody body(a);
  GamepadChangeEvent e(gamepadHandle, body);

  uint32_t id = ++mEventNumber;
  RefPtr<Promise> p = Promise::Create(mWindow->AsGlobal(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  MOZ_ASSERT(!mPromiseList.Contains(id));
  mPromiseList.InsertOrUpdate(id, RefPtr{p});
  mChild->SendGamepadTestEvent(id, e);
  return p.forget();
}

already_AddRefed<Promise> GamepadServiceTest::NewTouch(
    uint32_t aHandleSlot, uint32_t aTouchArrayIndex, uint32_t aTouchId,
    uint8_t aSurfaceId, const Float32Array& aPos,
    const Nullable<Float32Array>& aSurfDim, ErrorResult& aRv) {
  if (mShuttingDown) {
    aRv.ThrowInvalidStateError("Shutting down");
    return nullptr;
  }

  GamepadHandle gamepadHandle = GetHandleInSlot(aHandleSlot);

  GamepadTouchState touchState;
  touchState.touchId = aTouchId;
  touchState.surfaceId = aSurfaceId;
  DebugOnly<bool> ok = aPos.CopyDataTo(touchState.position);
  MOZ_ASSERT(ok, "aPos.Length() != std::size(touchState.position)");

  if (!aSurfDim.IsNull()) {
    ok = aSurfDim.Value().CopyDataTo(touchState.surfaceDimensions);
    MOZ_ASSERT(ok,
               "aSurfDim.Length() != std::size(touchState.surfaceDimensions)");
    touchState.isSurfaceDimensionsValid = true;
  }

  GamepadTouchInformation a(aTouchArrayIndex, touchState);
  GamepadChangeEventBody body(a);
  GamepadChangeEvent e(gamepadHandle, body);

  uint32_t id = ++mEventNumber;
  RefPtr<Promise> p = Promise::Create(mWindow->AsGlobal(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  MOZ_ASSERT(!mPromiseList.Contains(id));
  mPromiseList.InsertOrUpdate(id, RefPtr{p});
  mChild->SendGamepadTestEvent(id, e);
  return p.forget();
}

JSObject* GamepadServiceTest::WrapObject(JSContext* aCx,
                                         JS::Handle<JSObject*> aGivenProto) {
  return GamepadServiceTest_Binding::Wrap(aCx, this, aGivenProto);
}

uint32_t GamepadServiceTest::AddGamepadHandle(GamepadHandle aHandle) {
  uint32_t handleSlot = mGamepadHandles.Length();
  mGamepadHandles.AppendElement(aHandle);
  return handleSlot;
}

void GamepadServiceTest::RemoveGamepadHandle(uint32_t aHandleSlot) {
  MOZ_ASSERT(aHandleSlot < mGamepadHandles.Length());
  return mGamepadHandles.RemoveElementAt(aHandleSlot);
}

GamepadHandle GamepadServiceTest::GetHandleInSlot(uint32_t aHandleSlot) const {
  MOZ_ASSERT(aHandleSlot < mGamepadHandles.Length());
  return mGamepadHandles.ElementAt(aHandleSlot);
}

}  // namespace mozilla::dom
