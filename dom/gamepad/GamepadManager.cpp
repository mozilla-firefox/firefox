/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/GamepadManager.h"

#include "VRManagerChild.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Gamepad.h"
#include "mozilla/dom/GamepadAxisMoveEvent.h"
#include "mozilla/dom/GamepadButtonEvent.h"
#include "mozilla/dom/GamepadEvent.h"
#include "mozilla/dom/GamepadEventChannelChild.h"
#include "mozilla/dom/GamepadMonitoring.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "nsContentUtils.h"
#include "nsGlobalWindowInner.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsPIDOMWindowInlines.h"
#include "nsThreadUtils.h"

using namespace mozilla::ipc;

namespace mozilla::dom {

namespace {

const nsTArray<RefPtr<nsGlobalWindowInner>>::index_type NoIndex =
    nsTArray<RefPtr<nsGlobalWindowInner>>::NoIndex;

bool sShutdown = false;

StaticRefPtr<GamepadManager> gGamepadManagerSingleton;

// A threshold value of axis move to determine the first
// intent.
const float AXIS_FIRST_INTENT_THRESHOLD_VALUE = 0.1f;

}  // namespace

NS_IMPL_ISUPPORTS(GamepadManager, nsIObserver)

GamepadManager::GamepadManager()
    : mEnabled(false),
      mNonstandardEventsEnabled(false),
      mShuttingDown(false),
      mPromiseID(0) {}

nsresult GamepadManager::Init() {
  mEnabled = StaticPrefs::dom_gamepad_enabled();
  mNonstandardEventsEnabled =
      StaticPrefs::dom_gamepad_non_standard_events_enabled();
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();

  if (NS_WARN_IF(!observerService)) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv;
  rv = observerService->AddObserver(this, NS_XPCOM_WILL_SHUTDOWN_OBSERVER_ID,
                                    false);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

NS_IMETHODIMP
GamepadManager::Observe(nsISupports* aSubject, const char* aTopic,
                        const char16_t* aData) {
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService) {
    observerService->RemoveObserver(this, NS_XPCOM_WILL_SHUTDOWN_OBSERVER_ID);
  }
  BeginShutdown();
  return NS_OK;
}

void GamepadManager::StopMonitoring() {
  if (mChannelChild) {
    PGamepadEventChannelChild::Send__delete__(mChannelChild);
    mChannelChild = nullptr;
  }
  if (gfx::VRManagerChild::IsCreated()) {
    gfx::VRManagerChild* vm = gfx::VRManagerChild::Get();
    vm->SendControllerListenerRemoved();
  }
  mGamepads.Clear();
}

void GamepadManager::BeginShutdown() {
  mShuttingDown = true;
  StopMonitoring();
  // Don't let windows call back to unregister during shutdown
  for (uint32_t i = 0; i < mListeners.Length(); i++) {
    mListeners[i]->SetHasGamepadEventListener(false);
  }
  mListeners.Clear();
  sShutdown = true;
}

void GamepadManager::AddListener(nsGlobalWindowInner* aWindow) {
  MOZ_ASSERT(aWindow);
  MOZ_ASSERT(NS_IsMainThread());

  // IPDL child has not been created
  if (!mChannelChild) {
    PBackgroundChild* actor = BackgroundChild::GetOrCreateForCurrentThread();
    if (NS_WARN_IF(!actor)) {
      // We are probably shutting down.
      return;
    }

    RefPtr<GamepadEventChannelChild> child(GamepadEventChannelChild::Create());
    if (!actor->SendPGamepadEventChannelConstructor(child.get())) {
      // We are probably shutting down.
      return;
    }

    mChannelChild = child;

    if (gfx::VRManagerChild::IsCreated()) {
      // Construct VRManagerChannel and ask adding the connected
      // VR controllers to GamepadManager
      gfx::VRManagerChild* vm = gfx::VRManagerChild::Get();
      vm->SendControllerListenerAdded();
    }
  }

  if (!mEnabled || mShuttingDown ||
      aWindow->ShouldResistFingerprinting(RFPTarget::Gamepad)) {
    return;
  }

  if (mListeners.IndexOf(aWindow) != NoIndex) {
    return;  // already exists
  }

  mListeners.AppendElement(aWindow);
}

void GamepadManager::RemoveListener(nsGlobalWindowInner* aWindow) {
  MOZ_ASSERT(aWindow);

  if (mShuttingDown) {
    // Doesn't matter at this point. It's possible we're being called
    // as a result of our own destructor here, so just bail out.
    return;
  }

  if (mListeners.IndexOf(aWindow) == NoIndex) {
    return;  // doesn't exist
  }

  for (const auto& key : mGamepads.Keys()) {
    aWindow->RemoveGamepad(key);
  }

  mListeners.RemoveElement(aWindow);

  if (mListeners.IsEmpty()) {
    StopMonitoring();
  }
}

already_AddRefed<Gamepad> GamepadManager::GetGamepad(
    GamepadHandle aHandle) const {
  RefPtr<Gamepad> gamepad;
  if (mGamepads.Get(aHandle, getter_AddRefs(gamepad))) {
    return gamepad.forget();
  }

  return nullptr;
}

void GamepadManager::AddGamepad(GamepadHandle aHandle, const nsAString& aId,
                                GamepadMappingType aMapping,
                                uint32_t aNumButtons, uint32_t aNumAxes,
                                uint32_t aNumHaptics,
                                uint32_t aNumLightIndicator,
                                uint32_t aNumTouchEvents) {
  // TODO: bug 852258: get initial button/axis state
  RefPtr<Gamepad> newGamepad =
      new Gamepad(nullptr, aId,
                  0,  // index is set by global window
                  aHandle, aMapping, aNumButtons, aNumAxes, aNumHaptics,
                  aNumLightIndicator, aNumTouchEvents);

  // We store the gamepad related to its index given by the parent process,
  // and no duplicate index is allowed.
  MOZ_ASSERT(!mGamepads.Contains(aHandle));
  mGamepads.InsertOrUpdate(aHandle, std::move(newGamepad));
  NewConnectionEvent(aHandle, true);
}

void GamepadManager::RemoveGamepad(GamepadHandle aHandle) {
  RefPtr<Gamepad> gamepad = GetGamepad(aHandle);
  if (!gamepad) {
    NS_WARNING("Trying to delete gamepad with invalid index");
    return;
  }
  gamepad->SetConnected(false);
  NewConnectionEvent(aHandle, false);
  mGamepads.Remove(aHandle);
}

void GamepadManager::FireButtonEvent(EventTarget* aTarget, Gamepad* aGamepad,
                                     uint32_t aButton, double aValue) {
  nsString name =
      aValue == 1.0L ? u"gamepadbuttondown"_ns : u"gamepadbuttonup"_ns;
  GamepadButtonEventInit init;
  init.mBubbles = false;
  init.mCancelable = false;
  init.mGamepad = aGamepad;
  init.mButton = aButton;
  RefPtr<GamepadButtonEvent> event =
      GamepadButtonEvent::Constructor(aTarget, name, init);

  event->SetTrusted(true);

  aTarget->DispatchEvent(*event);
}

void GamepadManager::FireAxisMoveEvent(EventTarget* aTarget, Gamepad* aGamepad,
                                       uint32_t aAxis, double aValue) {
  GamepadAxisMoveEventInit init;
  init.mBubbles = false;
  init.mCancelable = false;
  init.mGamepad = aGamepad;
  init.mAxis = aAxis;
  init.mValue = aValue;
  RefPtr<GamepadAxisMoveEvent> event =
      GamepadAxisMoveEvent::Constructor(aTarget, u"gamepadaxismove"_ns, init);

  event->SetTrusted(true);

  aTarget->DispatchEvent(*event);
}

void GamepadManager::NewConnectionEvent(GamepadHandle aHandle,
                                        bool aConnected) {
  if (mShuttingDown) {
    return;
  }

  RefPtr<Gamepad> gamepad = GetGamepad(aHandle);
  if (!gamepad) {
    return;
  }

  // Hold on to listeners in a separate array because firing events
  // can mutate the mListeners array.
  nsTArray<RefPtr<nsGlobalWindowInner>> listeners(mListeners.Clone());

  if (aConnected) {
    for (uint32_t i = 0; i < listeners.Length(); i++) {
#ifdef NIGHTLY_BUILD
      // Don't fire a gamepadconnected event unless it's a secure context
      if (!listeners[i]->IsSecureContext()) {
        continue;
      }
#endif

      // Do not fire gamepadconnected and gamepaddisconnected events when
      // privacy.resistFingerprinting is true.
      if (listeners[i]->ShouldResistFingerprinting(RFPTarget::Gamepad)) {
        continue;
      }

      // Only send events to non-background windows
      if (!listeners[i]->IsCurrentInnerWindow() ||
          listeners[i]->GetOuterWindow()->IsBackground()) {
        continue;
      }

      // We don't fire a connected event here unless the window
      // has seen input from at least one device.
      if (!listeners[i]->HasSeenGamepadInput()) {
        continue;
      }

      SetWindowHasSeenGamepad(listeners[i], aHandle);

      RefPtr<Gamepad> listenerGamepad = listeners[i]->GetGamepad(aHandle);
      if (listenerGamepad) {
        // Fire event
        FireConnectionEvent(listeners[i], listenerGamepad, aConnected);
      }
    }
  } else {
    // For disconnection events, fire one at every window that has received
    // data from this gamepad.
    for (uint32_t i = 0; i < listeners.Length(); i++) {
      // Even background windows get these events, so we don't have to
      // deal with the hassle of syncing the state of removed gamepads.

      // Do not fire gamepadconnected and gamepaddisconnected events when
      // privacy.resistFingerprinting is true.
      if (listeners[i]->ShouldResistFingerprinting(RFPTarget::Gamepad)) {
        continue;
      }

      if (WindowHasSeenGamepad(listeners[i], aHandle)) {
        RefPtr<Gamepad> listenerGamepad = listeners[i]->GetGamepad(aHandle);
        if (listenerGamepad) {
          listenerGamepad->SetConnected(false);
          // Fire event
          FireConnectionEvent(listeners[i], listenerGamepad, false);
          listeners[i]->RemoveGamepad(aHandle);
        }
      }
    }
  }
}

void GamepadManager::FireConnectionEvent(EventTarget* aTarget,
                                         Gamepad* aGamepad, bool aConnected) {
  nsString name =
      aConnected ? u"gamepadconnected"_ns : u"gamepaddisconnected"_ns;
  GamepadEventInit init;
  init.mBubbles = false;
  init.mCancelable = false;
  init.mGamepad = aGamepad;
  RefPtr<GamepadEvent> event = GamepadEvent::Constructor(aTarget, name, init);

  event->SetTrusted(true);

  aTarget->DispatchEvent(*event);
}

void GamepadManager::SyncGamepadState(GamepadHandle aHandle,
                                      nsGlobalWindowInner* aWindow,
                                      Gamepad* aGamepad) {
  if (mShuttingDown || !mEnabled ||
      aWindow->ShouldResistFingerprinting(RFPTarget::Gamepad)) {
    return;
  }

  RefPtr<Gamepad> gamepad = GetGamepad(aHandle);
  if (!gamepad) {
    return;
  }

  aGamepad->SyncState(gamepad);
}

// static
bool GamepadManager::IsServiceRunning() { return !!gGamepadManagerSingleton; }

// static
already_AddRefed<GamepadManager> GamepadManager::GetService() {
  if (sShutdown) {
    return nullptr;
  }

  if (!gGamepadManagerSingleton) {
    RefPtr<GamepadManager> manager = new GamepadManager();
    nsresult rv = manager->Init();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nullptr;
    }
    gGamepadManagerSingleton = manager;
    ClearOnShutdown(&gGamepadManagerSingleton);
  }

  RefPtr<GamepadManager> service(gGamepadManagerSingleton);
  return service.forget();
}

bool GamepadManager::AxisMoveIsFirstIntent(nsGlobalWindowInner* aWindow,
                                           GamepadHandle aHandle,
                                           const GamepadChangeEvent& aEvent) {
  const GamepadChangeEventBody& body = aEvent.body();
  if (!WindowHasSeenGamepad(aWindow, aHandle) &&
      body.type() == GamepadChangeEventBody::TGamepadAxisInformation) {
    // Some controllers would send small axis values even they are just idle.
    // To avoid controllers be activated without its first intent.
    const GamepadAxisInformation& a = body.get_GamepadAxisInformation();
    if (abs(a.value()) < AXIS_FIRST_INTENT_THRESHOLD_VALUE) {
      return false;
    }
  }
  return true;
}

bool GamepadManager::EventContainsUserGesture(
    Gamepad* aGamepad, const GamepadChangeEvent& aEvent) {
  const GamepadChangeEventBody& body = aEvent.body();
  switch (body.type()) {
    case GamepadChangeEventBody::TGamepadButtonInformation: {
      const GamepadButtonInformation& a = body.get_GamepadButtonInformation();
      // Note: unlike the spec text, this treats an uninitialized button's
      // default `false` state the same as an explicitly-observed `false`
      // state, so a gamepad that connects with a button already held down
      // will count that button's very first `true` report as a gesture
      // (the spec says it SHOULD NOT, to avoid a pre-held button being
      // misread as an intentional press). Chrome doesn't implement that
      // exclusion either: GamepadComparisons::HasUserActivation() in
      // gamepad_comparisons.cc is a per-poll threshold check on button
      // value with no per-button history at all, so it's arguably more
      // permissive than this. No spec issue tracks the unenforced clause.
      GamepadButton* button = aGamepad->GetButton(a.button());
      return button && !button->Pressed() && a.pressed();
    }
    case GamepadChangeEventBody::TGamepadAxisInformation: {
      const GamepadAxisInformation& a = body.get_GamepadAxisInformation();
      double oldValue = 0.0;
      return aGamepad->GetAxis(a.axis(), &oldValue) &&
             abs(oldValue) < AXIS_FIRST_INTENT_THRESHOLD_VALUE &&
             abs(a.value()) >= AXIS_FIRST_INTENT_THRESHOLD_VALUE;
    }
    default:
      // The spec's "contains a gamepad user gesture" is defined only in
      // terms of buttons and axes.
      return false;
  }
}

bool GamepadManager::MaybeWindowHasSeenGamepad(nsGlobalWindowInner* aWindow,
                                               GamepadHandle aHandle) {
  if (!WindowHasSeenGamepad(aWindow, aHandle)) {
    // This window hasn't seen this gamepad before, so
    // send a connection event first.
    SetWindowHasSeenGamepad(aWindow, aHandle);
    return false;
  }
  return true;
}

bool GamepadManager::WindowHasSeenGamepad(nsGlobalWindowInner* aWindow,
                                          GamepadHandle aHandle) const {
  RefPtr<Gamepad> gamepad = aWindow->GetGamepad(aHandle);
  return gamepad != nullptr;
}

void GamepadManager::SetWindowHasSeenGamepad(nsGlobalWindowInner* aWindow,
                                             GamepadHandle aHandle,
                                             bool aHasSeen) {
  MOZ_ASSERT(aWindow);

  if (mListeners.IndexOf(aWindow) == NoIndex) {
    // This window isn't even listening for gamepad events.
    return;
  }

  if (aHasSeen) {
    aWindow->SetHasSeenGamepadInput(true);
    nsCOMPtr<nsISupports> window = ToSupports(aWindow);
    RefPtr<Gamepad> gamepad = GetGamepad(aHandle);
    if (!gamepad) {
      return;
    }
    RefPtr<Gamepad> clonedGamepad = gamepad->Clone(window);
    aWindow->AddGamepad(aHandle, clonedGamepad);
  } else {
    aWindow->RemoveGamepad(aHandle);
  }
}

void GamepadManager::Update(const GamepadChangeEvent& aEvent) {
  if (!mEnabled || mShuttingDown) {
    return;
  }

  const GamepadHandle handle = aEvent.handle();

  const GamepadChangeEventBody& body = aEvent.body();

  if (body.type() == GamepadChangeEventBody::TGamepadAdded) {
    const GamepadAdded& a = body.get_GamepadAdded();
    AddGamepad(handle, a.id(), static_cast<GamepadMappingType>(a.mapping()),
               a.num_buttons(), a.num_axes(), a.num_haptics(), a.num_lights(),
               a.num_touches());
    return;
  }
  if (body.type() == GamepadChangeEventBody::TGamepadRemoved) {
    RemoveGamepad(handle);
    return;
  }

  // https://w3c.github.io/gamepad/#dfn-gamepad-user-gesture
  // This is a property of the incoming hardware event against the
  // canonical gamepad's state, not of any particular window's view of it,
  // so compute it once, before SetGamepadByEvent() below overwrites that
  // state. (Each per-window clone is either seeded from this same
  // canonical state on first use, or already reflects the pre-event state
  // from its own prior updates, so re-deriving this per-window would
  // either see already-updated data or be redundant.)
  RefPtr<Gamepad> canonicalGamepad = GetGamepad(handle);
  bool containsUserGesture =
      canonicalGamepad && EventContainsUserGesture(canonicalGamepad, aEvent);

  if (!SetGamepadByEvent(aEvent)) {
    return;
  }

  // Hold on to listeners in a separate array because firing events
  // can mutate the mListeners array.
  nsTArray<RefPtr<nsGlobalWindowInner>> listeners(mListeners.Clone());

  for (uint32_t i = 0; i < listeners.Length(); i++) {
    // Only send events to non-background windows
    if (!listeners[i]->IsCurrentInnerWindow() ||
        listeners[i]->GetOuterWindow()->IsBackground() ||
        listeners[i]->ShouldResistFingerprinting(RFPTarget::Gamepad)) {
      continue;
    }

    SetGamepadByEvent(aEvent, listeners[i], containsUserGesture);
    MaybeConvertToNonstandardGamepadEvent(aEvent, listeners[i]);
  }
}

void GamepadManager::MaybeConvertToNonstandardGamepadEvent(
    const GamepadChangeEvent& aEvent, nsGlobalWindowInner* aWindow) {
  MOZ_ASSERT(aWindow);

  if (!mNonstandardEventsEnabled) {
    return;
  }

  GamepadHandle handle = aEvent.handle();

  RefPtr<Gamepad> gamepad = aWindow->GetGamepad(handle);
  const GamepadChangeEventBody& body = aEvent.body();

  if (gamepad) {
    switch (body.type()) {
      case GamepadChangeEventBody::TGamepadButtonInformation: {
        const GamepadButtonInformation& a = body.get_GamepadButtonInformation();
        FireButtonEvent(aWindow, gamepad, a.button(), a.value());
        break;
      }
      case GamepadChangeEventBody::TGamepadAxisInformation: {
        const GamepadAxisInformation& a = body.get_GamepadAxisInformation();
        FireAxisMoveEvent(aWindow, gamepad, a.axis(), a.value());
        break;
      }
      default:
        break;
    }
  }
}

bool GamepadManager::SetGamepadByEvent(const GamepadChangeEvent& aEvent,
                                       nsGlobalWindowInner* aWindow,
                                       bool aContainsUserGesture) {
  bool ret = false;
  bool firstTime = false;

  GamepadHandle handle = aEvent.handle();

  if (aWindow) {
    if (!AxisMoveIsFirstIntent(aWindow, handle, aEvent)) {
      return false;
    }
    firstTime = !MaybeWindowHasSeenGamepad(aWindow, handle);
  }

  RefPtr<Gamepad> gamepad =
      aWindow ? aWindow->GetGamepad(handle) : GetGamepad(handle);
  const GamepadChangeEventBody& body = aEvent.body();

  if (gamepad) {
    switch (body.type()) {
      case GamepadChangeEventBody::TGamepadButtonInformation: {
        const GamepadButtonInformation& a = body.get_GamepadButtonInformation();
        gamepad->SetButton(a.button(), a.pressed(), a.touched(), a.value());
        break;
      }
      case GamepadChangeEventBody::TGamepadAxisInformation: {
        const GamepadAxisInformation& a = body.get_GamepadAxisInformation();
        gamepad->SetAxis(a.axis(), a.value());
        break;
      }
      case GamepadChangeEventBody::TGamepadLightIndicatorTypeInformation: {
        const GamepadLightIndicatorTypeInformation& a =
            body.get_GamepadLightIndicatorTypeInformation();
        gamepad->SetLightIndicatorType(a.light(), a.type());
        break;
      }
      case GamepadChangeEventBody::TGamepadTouchInformation: {
        // Avoid GamepadTouch's touchId be accessed in cross-origin tracking.
        for (uint32_t i = 0; i < mListeners.Length(); i++) {
          RefPtr<Gamepad> listenerGamepad = mListeners[i]->GetGamepad(handle);
          if (listenerGamepad && mListeners[i]->IsCurrentInnerWindow() &&
              !mListeners[i]->GetOuterWindow()->IsBackground()) {
            const GamepadTouchInformation& a =
                body.get_GamepadTouchInformation();
            listenerGamepad->SetTouchEvent(a.index(), a.touch_state());
          }
        }
        break;
      }
      default:
        MOZ_ASSERT(false);
        break;
    }
    ret = true;
  }

  // https://html.spec.whatwg.org/multipage/interaction.html#activation-notification
  // Proposed as a new step of the "update gamepad state" algorithm by
  // https://github.com/w3c/gamepad/pull/230 (closes
  // https://github.com/w3c/gamepad/issues/124); not yet merged into the
  // published spec as of this writing. Run before the [[hasGamepadGesture]]
  // exposure/connection-event step below. Unlike that one-time latch, this
  // runs on every occurrence, matching how other activation-triggering
  // input events behave.
  //
  // https://html.spec.whatwg.org/multipage/interaction.html#fully-active-descendant-of-a-top-level-traversable-with-user-attention
  //   "A top-level traversable has user attention when its system
  //   visibility state is 'visible', and it either has system focus or
  //   user agent widgets directly related to it can receive keyboard
  //   input channeled from the operating system."
  // The caller already excludes background tabs, but that's only the
  // visibility half of "user attention": a foreground tab in a window the
  // OS hasn't focused would still pass it. GetIsActiveBrowserWindow()
  // covers the focus half, same as the pairing in nsDeviceSensors.cpp and
  // MediaDevices.cpp.
  if (aWindow && aContainsUserGesture &&
      aWindow->GetBrowsingContext()->Top()->GetIsActiveBrowserWindow()) {
    if (Document* doc = aWindow->GetExtantDoc()) {
      doc->NotifyUserGestureActivation();
    }
  }

  if (aWindow && firstTime) {
    FireConnectionEvent(aWindow, gamepad, true);
  }

  return ret;
}

already_AddRefed<Promise> GamepadManager::VibrateHaptic(
    GamepadHandle aHandle, uint32_t aHapticIndex, double aIntensity,
    double aDuration, nsIGlobalObject* aGlobal, ErrorResult& aRv) {
  RefPtr<Promise> promise = Promise::Create(aGlobal, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  if (StaticPrefs::dom_gamepad_haptic_feedback_enabled()) {
    if (aHandle.GetKind() == GamepadHandleKind::VR) {
      if (gfx::VRManagerChild::IsCreated()) {
        gfx::VRManagerChild* vm = gfx::VRManagerChild::Get();
        vm->AddPromise(mPromiseID, promise);
        vm->SendVibrateHaptic(aHandle, aHapticIndex, aIntensity, aDuration,
                              mPromiseID);
      }
    } else {
      if (mChannelChild) {
        mChannelChild->AddPromise(mPromiseID, promise);
        mChannelChild->SendVibrateHaptic(aHandle, aHapticIndex, aIntensity,
                                         aDuration, mPromiseID);
      }
    }
  }

  ++mPromiseID;
  return promise.forget();
}

void GamepadManager::StopHaptics() {
  if (!StaticPrefs::dom_gamepad_haptic_feedback_enabled()) {
    return;
  }

  for (const auto& entry : mGamepads) {
    const GamepadHandle handle = entry.GetRawPtr()->GetHandle();
    if (handle.GetKind() == GamepadHandleKind::VR) {
      if (gfx::VRManagerChild::IsCreated()) {
        gfx::VRManagerChild* vm = gfx::VRManagerChild::Get();
        vm->SendStopVibrateHaptic(handle);
      }
    } else {
      if (mChannelChild) {
        mChannelChild->SendStopVibrateHaptic(handle);
      }
    }
  }
}

already_AddRefed<Promise> GamepadManager::SetLightIndicatorColor(
    GamepadHandle aHandle, uint32_t aLightColorIndex, uint8_t aRed,
    uint8_t aGreen, uint8_t aBlue, nsIGlobalObject* aGlobal, ErrorResult& aRv) {
  RefPtr<Promise> promise = Promise::Create(aGlobal, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  if (StaticPrefs::dom_gamepad_extensions_lightindicator()) {
    MOZ_RELEASE_ASSERT(aHandle.GetKind() != GamepadHandleKind::VR,
                       "We don't support light indicator in VR.");

    if (mChannelChild) {
      mChannelChild->AddPromise(mPromiseID, promise);
      mChannelChild->SendLightIndicatorColor(aHandle, aLightColorIndex, aRed,
                                             aGreen, aBlue, mPromiseID);
    }
  }

  ++mPromiseID;
  return promise.forget();
}

already_AddRefed<Promise> GamepadManager::RequestAllGamepads(
    nsIGlobalObject* aGlobal, ErrorResult& aRv) {
  RefPtr<Promise> promise = Promise::Create(aGlobal, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  if (!mChannelChild) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  mChannelChild->SendRequestAllGamepads(
      [promise](const nsTArray<GamepadAdded>& aAddedGamepads) {
        nsTArray<RefPtr<Gamepad>> gamepads;

        for (const auto& addedGamepad : aAddedGamepads) {
          RefPtr<Gamepad> gamepad = new Gamepad(
              nullptr, addedGamepad.id(), 0, GamepadHandle(),
              addedGamepad.mapping(), addedGamepad.num_buttons(),
              addedGamepad.num_axes(), addedGamepad.num_haptics(),
              addedGamepad.num_lights(), addedGamepad.num_touches());
          gamepads.AppendElement(gamepad);
        }
        promise->MaybeResolve(gamepads);
      },
      [promise](mozilla::ipc::ResponseRejectReason) {
        promise->MaybeReject(NS_ERROR_UNEXPECTED);
      });

  return promise.forget();
}
}  // namespace mozilla::dom
