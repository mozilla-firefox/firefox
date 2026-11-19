/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_GamepadPlatformService_h_
#define mozilla_dom_GamepadPlatformService_h_

#include <map>

#include "mozilla/Mutex.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/Vector.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/GamepadBinding.h"
#include "mozilla/dom/GamepadEventTypes.h"
#include "mozilla/dom/GamepadHandle.h"

namespace mozilla::dom {

class GamepadEventChannelParent;
enum class GamepadLightIndicatorType : uint8_t;
struct GamepadPoseState;
class GamepadTestChannelParent;
struct GamepadTouchState;
class GamepadPlatformService;

class GamepadMonitoringState {
 public:
  static GamepadMonitoringState& GetSingleton();

  void AddObserver(GamepadTestChannelParent* aParent);
  void RemoveObserver(GamepadTestChannelParent* aParent);

  bool IsMonitoring() const;

  GamepadMonitoringState(const GamepadMonitoringState&) = delete;
  GamepadMonitoringState(GamepadMonitoringState&&) = delete;
  GamepadMonitoringState& operator=(const GamepadMonitoringState) = delete;
  GamepadMonitoringState& operator=(GamepadMonitoringState&&) = delete;

 private:
  GamepadMonitoringState() = default;
  ~GamepadMonitoringState() = default;

  void Set(bool aIsMonitoring);

  bool mIsMonitoring{false};
  Vector<WeakPtr<GamepadTestChannelParent>> mObservers;

  friend class mozilla::dom::GamepadPlatformService;
};

// Platform Service for building and transmitting IPDL messages
// through the HAL sandbox. Used by platform specific
// Gamepad implementations
//
// This class can be accessed by the following 2 threads :
// 1. Background thread:
//    This thread takes charge of IPDL communications
//    between here and DOM side
//
// 2. Monitor Thread:
//    This thread is populated in platform-dependent backends, which
//    is in charge of processing gamepad hardware events from OS
class GamepadPlatformService final {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(GamepadPlatformService)
 public:
  // Get the singleton service
  static already_AddRefed<GamepadPlatformService> GetParentService();

  // Add a gamepad to the list of known gamepads, and return its handle.
  GamepadHandle AddGamepad(const char* aID, GamepadMappingType aMapping,
                           GamepadHand aHand, uint32_t aNumButtons,
                           uint32_t aNumAxes, uint32_t aNumHaptics,
                           uint32_t aNumLightIndicator,
                           uint32_t aNumTouchEvents) MOZ_EXCLUDES(mMutex);
  // Remove the gamepad at |aHandle| from the list of known gamepads.
  void RemoveGamepad(GamepadHandle aHandle) MOZ_EXCLUDES(mMutex);

  // Update the state of |aButton| for the gamepad at |aHandle| for all
  // windows that are listening and visible, and fire one of
  // a gamepadbutton{up,down} event at them as well.
  // aPressed is used for digital buttons, aTouched is for detecting touched
  // events, aValue is for analog buttons.
  void NewButtonEvent(GamepadHandle aHandle, uint32_t aButton, bool aPressed,
                      bool aTouched, double aValue) MOZ_EXCLUDES(mMutex);
  // When only a digital button is available the value will be synthesized.
  void NewButtonEvent(GamepadHandle aHandle, uint32_t aButton, bool aPressed)
      MOZ_EXCLUDES(mMutex);
  // When only a digital button are available the value will be synthesized.
  void NewButtonEvent(GamepadHandle aHandle, uint32_t aButton, bool aPressed,
                      bool aTouched) MOZ_EXCLUDES(mMutex);
  // When only a digital button are available the value will be synthesized.
  void NewButtonEvent(GamepadHandle aHandle, uint32_t aButton, bool aPressed,
                      double aValue) MOZ_EXCLUDES(mMutex);
  // Update the state of |aAxis| for the gamepad at |aHandle| for all
  // windows that are listening and visible, and fire a gamepadaxismove
  // event at them as well.
  void NewAxisMoveEvent(GamepadHandle aHandle, uint32_t aAxis, double aValue)
      MOZ_EXCLUDES(mMutex);
  // Update the state of |aState| for the gamepad at |aHandle| for all
  // windows that are listening and visible.
  void NewPoseEvent(GamepadHandle aHandle, const GamepadPoseState& aState)
      MOZ_EXCLUDES(mMutex);
  // Update the type of |aType| for the gamepad at |aHandle| for all
  // windows that are listening and visible.
  void NewLightIndicatorTypeEvent(GamepadHandle aHandle, uint32_t aLight,
                                  GamepadLightIndicatorType aType)
      MOZ_EXCLUDES(mMutex);
  // Update the state of |aState| for the gamepad at |aHandle| with
  // |aTouchArrayIndex| for all windows that are listening and visible.
  void NewMultiTouchEvent(GamepadHandle aHandle, uint32_t aTouchArrayIndex,
                          const GamepadTouchState& aState) MOZ_EXCLUDES(mMutex);

  // When shutting down the platform communications for gamepad, also reset the
  // indexes.
  void ResetGamepadIndexes() MOZ_EXCLUDES(mMutex);

  // Add IPDL parent instance
  void AddChannelParent(GamepadEventChannelParent* aParent)
      MOZ_EXCLUDES(mMutex);

  // Remove IPDL parent instance
  void RemoveChannelParent(GamepadEventChannelParent* aParent)
      MOZ_EXCLUDES(mMutex);

  void MaybeShutdown() MOZ_EXCLUDES(mMutex);

  nsTArray<GamepadAdded> GetAllGamePads() MOZ_EXCLUDES(mMutex) {
    MutexAutoLock autoLock(mMutex);
    nsTArray<GamepadAdded> gamepads;

    for (const auto& elem : mGamepadAdded) {
      gamepads.AppendElement(elem.second);
    }
    return gamepads;
  }

 private:
  GamepadPlatformService();
  ~GamepadPlatformService();
  template <class T>
  void NotifyGamepadChange(GamepadHandle aHandle, const T& aInfo)
      MOZ_REQUIRES(mMutex);

  void Cleanup() MOZ_EXCLUDES(mMutex);

  // This mutex protects mNextGamepadHandleValue, mChannelParents, and
  // mGamepadAdded from race condition between background and monitor thread
  Mutex mMutex;

  uint32_t mNextGamepadHandleValue MOZ_GUARDED_BY(mMutex);

  // mChannelParents stores all the GamepadEventChannelParent instances
  // which may be accessed by both background thread and monitor thread
  // simultaneously
  nsTArray<RefPtr<GamepadEventChannelParent>> mChannelParents
      MOZ_GUARDED_BY(mMutex);

  std::map<GamepadHandle, GamepadAdded> mGamepadAdded MOZ_GUARDED_BY(mMutex);
};

}  // namespace mozilla::dom

#endif
