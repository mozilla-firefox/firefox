/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_HeadlessLookAndFeel_h
#define mozilla_widget_HeadlessLookAndFeel_h

#include "nsLookAndFeel.h"
#include "nsXPLookAndFeel.h"

namespace mozilla {
namespace widget {

#if defined(MOZ_WIDGET_GTK)

// Our nsLookAndFeel for Gtk relies on APIs that aren't available in headless
// mode, so for processes that are unable to connect to a display server, we use
// an implementation with hardcoded values.
//
// HeadlessLookAndFeel is used:
//
//   * in the parent process, when full headless mode (MOZ_HEADLESS=1) is
//     enabled
//
// The result of this is that when headless content mode is enabled, content
// processes use values derived from the parent's nsLookAndFeel (i.e., values
// derived from Gtk APIs) while still refraining from making any display server
// connections.

class HeadlessLookAndFeel : public nsXPLookAndFeel {
 public:
  explicit HeadlessLookAndFeel();
  virtual ~HeadlessLookAndFeel();

  void NativeInit() final {};
  nsresult NativeGetInt(IntID, int32_t& aResult) override;
  nsresult NativeGetFloat(FloatID, float& aResult) override;
  nsresult NativeGetColor(ColorID, ColorScheme, nscolor& aResult) override;
  bool NativeGetFont(FontID, nsString& aFontName, gfxFontStyle&) override;

  char16_t GetPasswordCharacterImpl() override;
};

#else

// When possible, we simply reuse the platform's existing nsLookAndFeel
// implementation in headless mode.

typedef nsLookAndFeel HeadlessLookAndFeel;

#endif

}  // namespace widget
}  // namespace mozilla

#endif
