/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Gamepad.h"

#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/GamepadBinding.h"
#include "nsPIDOMWindow.h"
#include "nsTArray.h"
#include "nsVariant.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTING_ADDREF(Gamepad)
NS_IMPL_CYCLE_COLLECTING_RELEASE(Gamepad)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Gamepad)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(Gamepad, mParent, mButtons, mPose,
                                      mHapticActuators, mLightIndicators,
                                      mTouchEvents)

void Gamepad::UpdateTimestamp() {
  nsCOMPtr<nsPIDOMWindowInner> newWindow(do_QueryInterface(mParent));
  if (newWindow) {
    Performance* perf = newWindow->GetPerformance();
    if (perf) {
      mTimestamp = perf->Now();
    }
  }
}

Gamepad::Gamepad(nsISupports* aParent, const nsAString& aID, int32_t aIndex,
                 GamepadHandle aHandle, GamepadMappingType aMapping,
                 GamepadHand aHand, uint32_t aNumButtons, uint32_t aNumAxes,
                 uint32_t aNumHaptics, uint32_t aNumLightIndicator,
                 uint32_t aNumTouchEvents)
    : mParent(aParent),
      mID(aID),
      mIndex(aIndex),
      mHandle(aHandle),
      mTouchIdHashValue(0),
      mMapping(aMapping),
      mHand(aHand),
      mConnected(true),
      mButtons(aNumButtons),
      mAxes(aNumAxes),
      mTimestamp(0) {
  for (unsigned i = 0; i < aNumButtons; i++) {
    mButtons.InsertElementAt(i, new GamepadButton(mParent));
  }
  mAxes.InsertElementsAt(0, aNumAxes, 0.0f);
  mPose = new GamepadPose(aParent);
  for (uint32_t i = 0; i < aNumHaptics; ++i) {
    mHapticActuators.AppendElement(
        new GamepadHapticActuator(mParent, mHandle, i));
  }
  for (uint32_t i = 0; i < aNumLightIndicator; ++i) {
    mLightIndicators.AppendElement(
        new GamepadLightIndicator(mParent, mHandle, i));
  }
  for (uint32_t i = 0; i < aNumTouchEvents; ++i) {
    mTouchEvents.AppendElement(new GamepadTouch(mParent));
  }

  // Mapping touchId(0) to touchIdHash(0) by default.
  mTouchIdHash.InsertOrUpdate(0, mTouchIdHashValue);
  ++mTouchIdHashValue;
  UpdateTimestamp();
}

void Gamepad::SetIndex(int32_t aIndex) { mIndex = aIndex; }

void Gamepad::SetConnected(bool aConnected) { mConnected = aConnected; }

void Gamepad::SetButton(uint32_t aButton, bool aPressed, bool aTouched,
                        double aValue) {
  // Until we fix the synchronization errors in Bug 1682554, this can be
  // called with a stale index that might overflow. In such a case, we silently
  // ignore it.
  if (aButton >= mButtons.Length()) {
    return;
  }
  mButtons[aButton]->SetPressed(aPressed);
  mButtons[aButton]->SetTouched(aTouched);
  mButtons[aButton]->SetValue(aValue);
  UpdateTimestamp();
}

void Gamepad::SetAxis(uint32_t aAxis, double aValue) {
  // Until we fix the synchronization errors in Bug 1682554, this can be
  // called with a stale index that might overflow. In such a case, we silently
  // ignore it.
  if (aAxis >= mAxes.Length()) {
    return;
  }

  if (mAxes[aAxis] != aValue) {
    mAxes[aAxis] = aValue;
    Gamepad_Binding::ClearCachedAxesValue(this);
  }
  UpdateTimestamp();
}

void Gamepad::SetPose(const GamepadPoseState& aPose) {
  mPose->SetPoseState(aPose);
  UpdateTimestamp();
}

void Gamepad::SetLightIndicatorType(uint32_t aLightIndex,
                                    GamepadLightIndicatorType aType) {
  // Until we fix the synchronization errors in Bug 1682554, this can be
  // called with a stale index that might overflow. In such a case, we silently
  // ignore it.
  if (aLightIndex >= mLightIndicators.Length()) {
    return;
  }

  mLightIndicators[aLightIndex]->SetType(aType);
  UpdateTimestamp();
}

void Gamepad::SetTouchEvent(uint32_t aTouchIndex,
                            const GamepadTouchState& aTouch) {
  // Until we fix the synchronization errors in Bug 1682554, this can be
  // called with a stale index that might overflow. In such a case, we silently
  // ignore it.
  if (aTouchIndex >= mTouchEvents.Length()) {
    return;
  }

  // Handling cross-origin tracking.
  GamepadTouchState touchState(aTouch);
  touchState.touchId = mTouchIdHash.LookupOrInsertWith(
      touchState.touchId, [&] { return mTouchIdHashValue++; });
  mTouchEvents[aTouchIndex]->SetTouchState(touchState);
  UpdateTimestamp();
}

void Gamepad::SetHand(GamepadHand aHand) { mHand = aHand; }

void Gamepad::SyncState(Gamepad* aOther) {
  if (mButtons.Length() != aOther->mButtons.Length() ||
      mAxes.Length() != aOther->mAxes.Length()) {
    return;
  }

  mConnected = aOther->mConnected;
  for (uint32_t i = 0; i < mButtons.Length(); ++i) {
    mButtons[i]->SetPressed(aOther->mButtons[i]->Pressed());
    mButtons[i]->SetTouched(aOther->mButtons[i]->Touched());
    mButtons[i]->SetValue(aOther->mButtons[i]->Value());
  }

  bool changed = false;
  for (uint32_t i = 0; i < mAxes.Length(); ++i) {
    changed = changed || (mAxes[i] != aOther->mAxes[i]);
    mAxes[i] = aOther->mAxes[i];
  }
  if (changed) {
    Gamepad_Binding::ClearCachedAxesValue(this);
  }

  if (StaticPrefs::dom_gamepad_pose_enabled()) {
    MOZ_ASSERT(aOther->GetPose());
    mPose->SetPoseState(aOther->GetPose()->GetPoseState());
  }
  if (StaticPrefs::dom_gamepad_hand_enabled()) {
    mHand = aOther->Hand();
  }
  if (StaticPrefs::dom_gamepad_haptic_actuators_enabled()) {
    for (uint32_t i = 0; i < mHapticActuators.Length(); ++i) {
      mHapticActuators[i]->Set(aOther->mHapticActuators[i]);
    }
  }
  if (StaticPrefs::dom_gamepad_extensions_lightindicator()) {
    for (uint32_t i = 0; i < mLightIndicators.Length(); ++i) {
      mLightIndicators[i]->Set(aOther->mLightIndicators[i]);
    }
  }
  if (StaticPrefs::dom_gamepad_extensions_multitouch()) {
    for (uint32_t i = 0; i < mTouchEvents.Length(); ++i) {
      mTouchEvents[i]->Set(aOther->mTouchEvents[i]);
    }
  }

  UpdateTimestamp();
}

already_AddRefed<Gamepad> Gamepad::Clone(nsISupports* aParent) {
  RefPtr<Gamepad> out =
      new Gamepad(aParent, mID, mIndex, mHandle, mMapping, mHand,
                  mButtons.Length(), mAxes.Length(), mHapticActuators.Length(),
                  mLightIndicators.Length(), mTouchEvents.Length());
  out->SyncState(this);
  return out.forget();
}

/* virtual */
JSObject* Gamepad::WrapObject(JSContext* aCx,
                              JS::Handle<JSObject*> aGivenProto) {
  return Gamepad_Binding::Wrap(aCx, this, aGivenProto);
}

}  // namespace mozilla::dom
