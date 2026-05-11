/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const PLATFORM_PREFS = (() => {
  if (AppConstants.platform == "win") {
    return ["widget.windows.mica"];
  }
  if (AppConstants.platform == "macosx") {
    return [
      "browser.theme.native-theme",
      "widget.macos.titlebar-blend-mode.behind-window",
    ];
  }
  return [];
})();

function applyNovaPlatformDefaults() {
  const on = Services.prefs.getBoolPref("browser.nova.enabled", false);
  const defaults = Services.prefs.getDefaultBranch("");
  for (const pref of PLATFORM_PREFS) {
    defaults.setBoolPref(pref, on);
  }
}

export const NovaPrefs = {
  init() {
    if (!PLATFORM_PREFS.length) {
      return;
    }
    applyNovaPlatformDefaults();
    Services.prefs.addObserver(
      "browser.nova.enabled",
      applyNovaPlatformDefaults
    );
  },
};
