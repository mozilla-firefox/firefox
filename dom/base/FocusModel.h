/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_FocusModel_h
#define mozilla_FocusModel_h

#include "mozilla/LookAndFeel.h"
#include "mozilla/StaticPrefs_accessibility.h"
#include "mozilla/TypedEnumBits.h"

namespace mozilla {

enum class IsFocusableFlags : uint8_t {
  WithMouse = 1 << 0,
  // Whether to treat an invisible frame as not focusable
  IgnoreVisibility = 1 << 1,
  IgnoreDisabled = 1 << 2,
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(IsFocusableFlags);

enum class TabFocusableType : uint8_t {
  TextControls = 1,       // Textboxes and lists always tabbable
  FormElements = 1 << 1,  // Non-text form elements
  Links = 1 << 2,         // Links
  Any = TextControls | FormElements | Links,
};

class FocusModel final {
 public:
  static bool AppliesToXUL() {
    return StaticPrefs::accessibility_tabfocus_applies_to_xul();
  }

  static bool IsTabFocusable(TabFocusableType aType) {
    if (StaticPrefs::accessibility_tabfocus() & int32_t(aType)) {
      return true;
    }
    if (LookAndFeel::GetInt(LookAndFeel::IntID::FullKeyboardAccess)) {
      return true;
    }
    return false;
  }
};

}  // namespace mozilla

#endif
