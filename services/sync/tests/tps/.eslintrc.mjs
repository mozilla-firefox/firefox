/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export default [
  {
    languageOptions: {
      globals: {
        // Injected into tests via tps.sys.mjs
        Addons: false,
        Addresses: false,
        Bookmarks: false,
        CreditCards: false,
        EnableEngines: false,
        EnsureTracking: false,
        ExtStorage: false,
        Formdata: false,
        History: false,
        Login: false,
        Passwords: false,
        Phase: false,
        Prefs: false,
        STATE_DISABLED: false,
        STATE_ENABLED: false,
        Sync: false,
        Tabs: false,
        Windows: false,
        WipeServer: false,
      },
    },
  },
];
