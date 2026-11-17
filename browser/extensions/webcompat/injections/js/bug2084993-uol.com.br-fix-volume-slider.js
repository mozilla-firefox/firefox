/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Bug 1878024 - volume slider for videos does not work.
 *
 * The site is relying on non-standard pseudo-element behavior in Chrome/Webkit
 * to draw their own slider track. We un-hide the "real" track as a work-around.
 */

if (!window.__firefoxWebCompatFixBug1878024) {
  Object.defineProperty(window, "__firefoxWebCompatFixBug1878024", {
    configurable: false,
    value: true,
  });

  const ENABLED_MESSAGE =
    "Extra CSS is being applied to fix the video volume slider in the news-player web component. See https://bugzil.la/1878024 for details.";

  const { appendChild } = DocumentFragment.prototype;

  ShadowRoot.prototype.appendChild = function (child) {
    if (this.host?.localName === "news-player") {
      if (child.nodeName == "STYLE") {
        console.info(ENABLED_MESSAGE);
        child.textContent = `${child.textContent} .volume-range input[type="range"].svelte-1ytunfe { background: var(--track-color); }`;
        delete ShadowRoot.prototype.appendChild;
      }
    }
    return appendChild.call(this, child);
  };
}
