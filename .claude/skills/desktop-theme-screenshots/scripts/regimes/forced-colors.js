/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* exported setupRegime */

// Windows high contrast puts chrome in the `requested` state, where no color
// is rewritten; the override reaches `active`, and forced-color-adjust: none
// on the root stops the rewriting (css_guidelines "Chrome area").
async function setupRegime() {
  window.browsingContext.forcedColorsOverride = "active";
  window.document.documentElement.style.forcedColorAdjust = "none";
  await new Promise(resolve => window.setTimeout(resolve, 300));
  if (!window.matchMedia("(forced-colors)").matches) {
    throw new Error("regime did not take: (forced-colors) does not match");
  }
  return { forcedColors: true };
}
