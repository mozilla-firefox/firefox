/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Bug 1956760 - sandboxels.r74n.com - Glow effect does not work
 *
 * The optional glow.js mod only enables itself when window.chrome is
 * defined. To avoid exposing window.chrome to the rest of the page, we
 * only define it while the mod's script is being loaded and run.
 */

if (!window.__firefoxWebCompatFixBug1956760) {
  Object.defineProperty(window, "__firefoxWebCompatFixBug1956760", {
    configurable: false,
    value: true,
  });

  const isGlowMod = node =>
    node instanceof HTMLScriptElement &&
    /\/glow\.js$/.test(new URL(node.src || "/", location.href).pathname);

  new MutationObserver(mutations => {
    for (const { addedNodes } of mutations) {
      for (const node of addedNodes) {
        if (!isGlowMod(node) || window.chrome) {
          continue;
        }
        console.info(
          "window.chrome has been defined for the glow.js mod for compatibility reasons. See https://bugzil.la/1956760 for details."
        );
        window.chrome = {};
        const cleanup = () => delete window.chrome;
        node.addEventListener("load", cleanup, { once: true });
        node.addEventListener("error", cleanup, { once: true });
      }
    }
  }).observe(document, { childList: true, subtree: true });
}
