/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* globals browser */

// To enable logging, set extensions.webcompat.debug_logging=true and restart.

const loggingPrefValue = browser.aboutConfigPrefs.getPref(
  "debug_logging",
  false
);

const debugLog = (function () {
  if (!loggingPrefValue) {
    return () => {};
  }
  if (browser.appConstants.getPlatform() == "android") {
    // Escape console log entries ourselves so the remote debugger does not spam [Object object] (see bz2074120)
    return function () {
      console.debug.apply(
        this,
        [...arguments].map(a => (typeof a == "string" ? a : JSON.stringify(a)))
      );
    };
  }
  return console.debug;
})();
