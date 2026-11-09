/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_KeyboardEvent_h_
#define mozilla_dom_KeyboardEvent_h_

#include "mozilla/EventForwards.h"
#include "mozilla/dom/KeyboardEventBinding.h"
#include "mozilla/dom/UIEvent.h"

namespace mozilla::dom {

class KeyboardEvent : public UIEvent {
 public:
  KeyboardEvent(EventTarget* aOwner, nsPresContext* aPresContext,
                WidgetKeyboardEvent* aEvent);

  NS_INLINE_DECL_REFCOUNTING_INHERITED(KeyboardEvent, UIEvent)

  virtual KeyboardEvent* AsKeyboardEvent() override { return this; }

  static already_AddRefed<KeyboardEvent> ConstructorJS(
      const GlobalObject& aGlobal, const nsAString& aType,
      const KeyboardEventInit& aParam);

  virtual JSObject* WrapObjectInternal(
      JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override {
    return KeyboardEvent_Binding::Wrap(aCx, this, aGivenProto);
  }

  bool AltKey(CallerType aCallerType = CallerType::System);
  bool CtrlKey(CallerType aCallerType = CallerType::System);
  bool ShiftKey(CallerType aCallerType = CallerType::System);
  bool MetaKey();

  // Returns true if the modifier state of the event matches the modifier state
  // of access key.
  bool IsMenuAccessKeyPressed() const;
  Modifiers GetModifiersForMenuAccessKey() const;

  bool GetModifierState(const nsAString& aKey,
                        CallerType aCallerType = CallerType::System) {
    bool modifierState = GetModifierStateInternal(aKey);

    if (!ShouldResistFingerprinting(aCallerType)) {
      return modifierState;
    }

    Modifiers modifier = WidgetInputEvent::GetModifier(aKey);
    return GetSpoofedModifierStates(modifier, modifierState);
  }

  bool Repeat();
  bool IsComposing();
  void GetKey(nsAString& aKey) const;
  uint32_t CharCode(CallerType aCallerType = CallerType::System);
  uint32_t KeyCode(CallerType aCallerType = CallerType::System);
  virtual uint32_t Which(CallerType aCallerType = CallerType::System) override;
  uint32_t Location();

  void GetCode(nsAString& aCode, CallerType aCallerType = CallerType::System);
  void GetInitDict(KeyboardEventInit& aParam);

  void InitKeyboardEventJS(const nsAString& aType, bool aCanBubble,
                           bool aCancelable, nsGlobalWindowInner* aView,
                           const nsAString& aKey, uint32_t aLocation,
                           bool aCtrlKey, bool aAltKey, bool aShiftKey,
                           bool aMetaKey);

  void GetRegisterableShortcutKeys(
      nsTArray<RegisterableShortcutKey>& aCandidates) const;

 protected:
  ~KeyboardEvent() = default;

 private:
  // True, if the instance is initialized by JS.
  bool mInitializedByJS;
  // True, if the instance is initialized by Ctor.
  bool mInitializedByCtor;

  // If the instance is created with Constructor(), which may have independent
  // value.  mInitializedWhichValue stores it.  I.e., this is invalid when
  // mInitializedByCtor is false.
  uint32_t mInitializedWhichValue;

  // This method returns the boolean to indicate whether spoofing keyboard
  // event for fingerprinting resistance. It will return true when pref
  // 'privacy.resistFingerprinting' is true and the event target is content.
  // Otherwise, it will return false.
  bool ShouldResistFingerprinting(CallerType aCallerType);

  // This method returns the spoofed modifier state of the given modifier key
  // for fingerprinting resistance.
  bool GetSpoofedModifierStates(const Modifiers aModifierKey,
                                const bool aRawModifierState);

  /**
   * ComputeTraditionalKeyCode() computes traditional keyCode value.  I.e.,
   * returns 0 if this event should return non-zero from CharCode().
   * In spite of the name containing "traditional", this computes spoof
   * keyCode value if user wants it.
   *
   * @param aKeyboardEvent  Should be |*mEvent->AsKeyboardEvent()|.
   * @param aCallerType     Set caller type of KeyCode() or CharCode().
   * @return                If traditional charCode value is 0, returns
   *                        the raw keyCode value or spoof keyCode value.
   *                        Otherwise, 0.
   */
  uint32_t ComputeTraditionalKeyCode(WidgetKeyboardEvent& aKeyboardEvent,
                                     CallerType aCallerType);
  /**
   * ShouldUseSameValueForCharCodeAndKeyCode() returns true if KeyCode() and
   * CharCode() should return same value.
   */
  bool ShouldUseSameValueForCharCodeAndKeyCode(
      const WidgetKeyboardEvent& aKeyboardEvent, CallerType aCallerType) const;
};

}  // namespace mozilla::dom

already_AddRefed<mozilla::dom::KeyboardEvent> NS_NewDOMKeyboardEvent(
    mozilla::dom::EventTarget* aOwner, nsPresContext* aPresContext,
    mozilla::WidgetKeyboardEvent* aEvent);

#endif  // mozilla_dom_KeyboardEvent_h_
