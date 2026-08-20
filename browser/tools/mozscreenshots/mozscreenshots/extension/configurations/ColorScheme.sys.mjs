/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line no-shadow
import { AddonManager } from "resource://gre/modules/AddonManager.sys.mjs";

export var ColorScheme = {
  init() {},

  configurations: {
    light: {
      selectors: [],
      async applyConfig() {
        Services.prefs.setIntPref("ui.systemUsesDarkTheme", 0);
        let addon = await AddonManager.getAddonByID(
          "default-theme@mozilla.org"
        );
        await addon.enable();
      },
    },

    dark: {
      selectors: [],
      async applyConfig() {
        Services.prefs.setIntPref("ui.systemUsesDarkTheme", 1);
        let addon = await AddonManager.getAddonByID(
          "firefox-compact-dark@mozilla.org"
        );
        await addon.enable();
      },
    },
  },
};
