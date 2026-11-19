/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_gamepad_Gamepad_h
#define mozilla_dom_gamepad_Gamepad_h

#include <stdint.h>

#include "mozilla/dom/GamepadBinding.h"
#include "mozilla/dom/GamepadButton.h"
#include "mozilla/dom/GamepadHandle.h"
#include "mozilla/dom/GamepadHapticActuator.h"
#include "mozilla/dom/GamepadLightIndicator.h"
#include "mozilla/dom/GamepadPose.h"
#include "mozilla/dom/GamepadTouch.h"
#include "mozilla/dom/Performance.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsTHashMap.h"
#include "nsWrapperCache.h"

namespace mozilla::dom {

class GamepadHapticActuator;

// Per spec:
// https://dvcs.w3.org/hg/gamepad/raw-file/default/gamepad.html#remapping
const int kStandardGamepadButtons = 17;
const int kStandardGamepadAxes = 4;

const int kButtonLeftTrigger = 6;
const int kButtonRightTrigger = 7;

const int kLeftStickXAxis = 0;
const int kLeftStickYAxis = 1;
const int kRightStickXAxis = 2;
const int kRightStickYAxis = 3;

class Gamepad final : public nsISupports, public nsWrapperCache {
 public:
  Gamepad(nsISupports* aParent, const nsAString& aID, int32_t aIndex,
          GamepadHandle aHandle, GamepadMappingType aMapping, GamepadHand aHand,
          uint32_t aNumButtons, uint32_t aNumAxes, uint32_t aNumHaptics,
          uint32_t aNumLightIndicator, uint32_t aNumTouchEvents);

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(Gamepad)

  void SetConnected(bool aConnected);
  void SetButton(uint32_t aButton, bool aPressed, bool aTouched, double aValue);
  void SetAxis(uint32_t aAxis, double aValue);
  void SetIndex(int32_t aIndex);
  void SetPose(const GamepadPoseState& aPose);
  void SetLightIndicatorType(uint32_t aLightIndex,
                             GamepadLightIndicatorType aType);
  void SetTouchEvent(uint32_t aTouchIndex, const GamepadTouchState& aTouch);
  void SetHand(GamepadHand aHand);

  // Make the state of this gamepad equivalent to other.
  void SyncState(Gamepad* aOther);

  // Return a new Gamepad containing the same data as this object,
  // parented to aParent.
  already_AddRefed<Gamepad> Clone(nsISupports* aParent);

  nsISupports* GetParentObject() const { return mParent; }

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

  void GetId(nsAString& aID) const { aID = mID; }

  DOMHighResTimeStamp Timestamp() const { return mTimestamp; }

  GamepadMappingType Mapping() { return mMapping; }

  GamepadHand Hand() { return mHand; }

  bool Connected() const { return mConnected; }

  int32_t Index() const { return mIndex; }

  void GetButtons(nsTArray<RefPtr<GamepadButton>>& aButtons) const {
    aButtons = mButtons.Clone();
  }

  // Returns nullptr if aButton is out of range.
  GamepadButton* GetButton(uint32_t aButton) const {
    return aButton < mButtons.Length() ? mButtons[aButton].get() : nullptr;
  }

  void GetAxes(nsTArray<double>& aAxes) const { aAxes = mAxes.Clone(); }

  // Returns false (and leaves *aOutValue untouched) if aAxis is out of
  // range.
  bool GetAxis(uint32_t aAxis, double* aOutValue) const {
    if (aAxis >= mAxes.Length()) {
      return false;
    }
    *aOutValue = mAxes[aAxis];
    return true;
  }

  GamepadPose* GetPose() const { return mPose; }

  void GetHapticActuators(
      nsTArray<RefPtr<GamepadHapticActuator>>& aHapticActuators) const {
    aHapticActuators = mHapticActuators.Clone();
  }

  void GetLightIndicators(
      nsTArray<RefPtr<GamepadLightIndicator>>& aLightIndicators) const {
    aLightIndicators = mLightIndicators.Clone();
  }

  void GetTouchEvents(nsTArray<RefPtr<GamepadTouch>>& aTouchEvents) const {
    aTouchEvents = mTouchEvents.Clone();
  }

  GamepadHandle GetHandle() const { return mHandle; }

 private:
  virtual ~Gamepad() = default;
  void UpdateTimestamp();

 protected:
  nsCOMPtr<nsISupports> mParent;
  nsString mID;
  int32_t mIndex;
  // the gamepad hash key in GamepadManager
  GamepadHandle mHandle;
  uint32_t mTouchIdHashValue;
  // The mapping in use.
  GamepadMappingType mMapping;
  GamepadHand mHand;

  // true if this gamepad is currently connected.
  bool mConnected;

  // Current state of buttons, axes.
  nsTArray<RefPtr<GamepadButton>> mButtons;
  nsTArray<double> mAxes;
  DOMHighResTimeStamp mTimestamp;
  RefPtr<GamepadPose> mPose;
  nsTArray<RefPtr<GamepadHapticActuator>> mHapticActuators;
  nsTArray<RefPtr<GamepadLightIndicator>> mLightIndicators;
  nsTArray<RefPtr<GamepadTouch>> mTouchEvents;
  nsTHashMap<nsUint32HashKey, uint32_t> mTouchIdHash;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_gamepad_Gamepad_h
