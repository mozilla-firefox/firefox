/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */

[Exposed=Window]
interface KeyboardEvent : UIEvent
{
  [BinaryName="constructorJS"]
  constructor(DOMString typeArg,
              optional KeyboardEventInit keyboardEventInitDict= {});

  [NeedsCallerType]
  readonly attribute unsigned long    charCode;
  [NeedsCallerType]
  readonly attribute unsigned long    keyCode;

  [NeedsCallerType]
  readonly attribute boolean          altKey;
  [NeedsCallerType]
  readonly attribute boolean          ctrlKey;
  [NeedsCallerType]
  readonly attribute boolean          shiftKey;
  readonly attribute boolean          metaKey;

  [NeedsCallerType]
  boolean getModifierState(DOMString key);

  const unsigned long DOM_KEY_LOCATION_STANDARD = 0x00;
  const unsigned long DOM_KEY_LOCATION_LEFT     = 0x01;
  const unsigned long DOM_KEY_LOCATION_RIGHT    = 0x02;
  const unsigned long DOM_KEY_LOCATION_NUMPAD   = 0x03;

  readonly attribute unsigned long location;
  readonly attribute boolean       repeat;
  readonly attribute boolean       isComposing;

  readonly attribute DOMString key;
  [NeedsCallerType]
  readonly attribute DOMString code;

  [BinaryName="initKeyboardEventJS"]
  undefined initKeyboardEvent(DOMString typeArg,
                              optional boolean bubblesArg = false,
                              optional boolean cancelableArg = false,
                              optional Window? viewArg = null,
                              optional DOMString keyArg = "",
                              optional unsigned long locationArg = 0,
                              optional boolean ctrlKey = false,
                              optional boolean altKey = false,
                              optional boolean shiftKey = false,
                              optional boolean metaKey = false);

  // This returns the initialized dictionary for generating a
  // same-type keyboard event
  [Cached, ChromeOnly, Constant]
  readonly attribute KeyboardEventInit initDict;

  // Return an array of RegisterableShortcutKey dictionaries whose fields
  // represents a shortcut key which matches with the event. The first one in
  // the array is the most preferred shortcut which would be accessible with
  // any keyboard layouts. NOTE: users may install multiple keyboard layouts,
  // for example, if the user's language text is written with Unicode characters
  // (like Greek, Cyrillic, Arabic, Hebrew), they installed an ASCII capable
  // keyboard layout too. ASCII alphabet should be accessible with both keyboard
  // layouts, but ASCII punctuations may be accessible with different Shift
  // state or completely inaccessible. So, users need to choose for their
  // environment from the result.
  //
  // For example,
  // { key: "A", altKey: true, ctrlKey: true }:  Alt + Ctrl + A
  // { key: "/", altKey: true, shiftKey: true }: Alt + Shift + /
  // { key: ";", metaKey: true }: Meta + ;
  [ChromeOnly]
  sequence<RegisterableShortcutKey> getRegisterableShortcutKeys();
};

dictionary KeyboardEventInit : EventModifierInit
{
  [BinaryType="nsAutoString"]
  DOMString      key           = "";
  [BinaryType="nsAutoString"]
  DOMString      code          = "";
  unsigned long  location      = 0;
  boolean        repeat        = false;
  boolean        isComposing   = false;

  // legacy attributes
  unsigned long  charCode      = 0;
  unsigned long  keyCode       = 0;
  unsigned long  which         = 0;
};

// See KeyboardEvent.getRegisterableShortcutKeys()
dictionary RegisterableShortcutKey {
  unsigned long keyCode = 0;
  DOMString key = "";
  boolean altKey = false;
  boolean ctrlKey = false;
  boolean metaKey = false;
  boolean shiftKey = false;
};

// Mozilla extensions
KeyboardEvent includes KeyEventMixin;
