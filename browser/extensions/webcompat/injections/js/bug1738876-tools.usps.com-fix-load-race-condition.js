/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Bug 1738876 - Page is intermittently stuck in a loading state.
 *
 * The site defines a variable in a jQuery $(document).ready() callback and
 * uses it in a $(window).on("load") handler. jQuery runs ready callbacks in
 * a setTimeout after DOMContentLoaded, so the load event can win the race,
 * throwing a ReferenceError and leaving the loading overlay up forever.
 * We hold back the window load event until jQuery's ready callbacks ran.
 */

if (!window.__firefoxWebCompatFixBug1738876) {
  Object.defineProperty(window, "__firefoxWebCompatFixBug1738876", {
    configurable: false,
    value: true,
  });

  let deferred = false;
  window.addEventListener(
    "load",
    e => {
      if (deferred || !e.isTrusted || e.target !== document || !window.jQuery) {
        return;
      }
      deferred = true;
      e.stopImmediatePropagation();
      console.info(
        "The window load event has been deferred until jQuery is ready for compatibility reasons. See https://bugzil.la/1738876 for details."
      );
      window.jQuery(() => {
        window.dispatchEvent(new Event("load"));
      });
    },
    true
  );
}
