/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_GamepadManager_h_
#define mozilla_dom_GamepadManager_h_

#include "nsIObserver.h"
#include "nsRefPtrHashtable.h"
// Needed for GamepadMappingType

#include "mozilla/dom/GamepadBinding.h"
#include "mozilla/dom/GamepadHandle.h"

class nsGlobalWindowInner;
class nsIGlobalObject;

namespace mozilla {
namespace gfx {
class VRManagerChild;
}  // namespace gfx
namespace dom {

class EventTarget;
class Gamepad;
class GamepadChangeEvent;
class GamepadEventChannelChild;

class GamepadManager final : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  // Returns true if we actually have a service up and running
  static bool IsServiceRunning();
  // Get the singleton service
  static already_AddRefed<GamepadManager> GetService();

  void BeginShutdown();
  void StopMonitoring();

  // Indicate that |aWindow| wants to receive gamepad events.
  void AddListener(nsGlobalWindowInner* aWindow);
  // Indicate that |aWindow| should no longer receive gamepad events.
  void RemoveListener(nsGlobalWindowInner* aWindow);

  // Add a gamepad to the list of known gamepads.
  void AddGamepad(GamepadHandle aHandle, const nsAString& aID,
                  GamepadMappingType aMapping, GamepadHand aHand,
                  uint32_t aNumButtons, uint32_t aNumAxes, uint32_t aNumHaptics,
                  uint32_t aNumLightIndicator, uint32_t aNumTouchEvents);

  // Remove the gamepad at |aIndex| from the list of known gamepads.
  void RemoveGamepad(GamepadHandle aHandle);

  // Synchronize the state of |aGamepad| to match the gamepad stored at |aIndex|
  void SyncGamepadState(GamepadHandle aHandle, nsGlobalWindowInner* aWindow,
                        Gamepad* aGamepad);

  // Returns gamepad object if index exists, null otherwise
  already_AddRefed<Gamepad> GetGamepad(GamepadHandle aHandle) const;

  // Receive GamepadChangeEvent messages from parent process to fire DOM events
  void Update(const GamepadChangeEvent& aGamepadEvent);

  // Trigger vibrate haptic event to gamepad channels.
  already_AddRefed<Promise> VibrateHaptic(GamepadHandle aHandle,
                                          uint32_t aHapticIndex,
                                          double aIntensity, double aDuration,
                                          nsIGlobalObject* aGlobal,
                                          ErrorResult& aRv);
  // Send stop haptic events to gamepad channels.
  void StopHaptics();

  // Set light indicator color event to gamepad channels.
  already_AddRefed<Promise> SetLightIndicatorColor(GamepadHandle aHandle,
                                                   uint32_t aLightColorIndex,
                                                   uint8_t aRed, uint8_t aGreen,
                                                   uint8_t aBlue,
                                                   nsIGlobalObject* aGlobal,
                                                   ErrorResult& aRv);

  // Request information of all gamepads from the parent process.
  already_AddRefed<Promise> RequestAllGamepads(nsIGlobalObject* aGlobal,
                                               ErrorResult& aRv);

 protected:
  GamepadManager();
  ~GamepadManager() = default;

  // Fire a gamepadconnected or gamepaddisconnected event for the gamepad
  // at |aIndex| to all windows that are listening and have received
  // gamepad input.
  void NewConnectionEvent(GamepadHandle aHandle, bool aConnected);

  // Fire a gamepadaxismove event to the window at |aTarget| for |aGamepad|.
  void FireAxisMoveEvent(EventTarget* aTarget, Gamepad* aGamepad, uint32_t axis,
                         double value);

  // Fire one of gamepadbutton{up,down} event at the window at |aTarget| for
  // |aGamepad|.
  void FireButtonEvent(EventTarget* aTarget, Gamepad* aGamepad,
                       uint32_t aButton, double aValue);

  // Fire one of gamepad{connected,disconnected} event at the window at
  // |aTarget| for |aGamepad|.
  void FireConnectionEvent(EventTarget* aTarget, Gamepad* aGamepad,
                           bool aConnected);

  // true if this feature is enabled in preferences
  bool mEnabled;
  // true if non-standard events are enabled in preferences
  bool mNonstandardEventsEnabled;
  // true when shutdown has begun
  bool mShuttingDown;

  RefPtr<GamepadEventChannelChild> mChannelChild;

 private:
  nsresult Init();

  void MaybeConvertToNonstandardGamepadEvent(const GamepadChangeEvent& aEvent,
                                             nsGlobalWindowInner* aWindow);

  bool SetGamepadByEvent(const GamepadChangeEvent& aEvent,
                         nsGlobalWindowInner* aWindow = nullptr,
                         bool aContainsUserGesture = false);
  // To avoid unintentionally causing the gamepad be activated.
  // Returns false if this gamepad hasn't been seen by this window
  // and the axis move data is less than AXIS_FIRST_INTENT_THRESHOLD_VALUE.
  bool AxisMoveIsFirstIntent(nsGlobalWindowInner* aWindow,
                             GamepadHandle aHandle,
                             const GamepadChangeEvent& aEvent);
  // https://w3c.github.io/gamepad/#dfn-gamepad-user-gesture
  // Returns true if applying |aEvent| to |aGamepad| is a gamepad user
  // gesture: a button press transition, or an axis move crossing
  // AXIS_FIRST_INTENT_THRESHOLD_VALUE. Must be called before the
  // corresponding SetButton()/SetAxis() call overwrites |aGamepad|'s
  // previous state, since it compares against that state.
  bool EventContainsUserGesture(Gamepad* aGamepad,
                                const GamepadChangeEvent& aEvent);
  bool MaybeWindowHasSeenGamepad(nsGlobalWindowInner* aWindow,
                                 GamepadHandle aHandle);
  // Returns true if we have already sent data from this gamepad
  // to this window. This should only return true if the user
  // explicitly interacted with a gamepad while this window
  // was focused, by pressing buttons or similar actions.
  bool WindowHasSeenGamepad(nsGlobalWindowInner* aWindow,
                            GamepadHandle aHandle) const;
  // Indicate that a window has received data from a gamepad.
  void SetWindowHasSeenGamepad(nsGlobalWindowInner* aWindow,
                               GamepadHandle aHandle, bool aHasSeen = true);

  // Gamepads connected to the system. Copies of these are handed out
  // to each window.
  nsRefPtrHashtable<nsGenericHashKey<GamepadHandle>, Gamepad> mGamepads;
  // Inner windows that are listening for gamepad events.
  // has been sent to that window.
  nsTArray<RefPtr<nsGlobalWindowInner>> mListeners;
  uint32_t mPromiseID;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_GamepadManager_h_
