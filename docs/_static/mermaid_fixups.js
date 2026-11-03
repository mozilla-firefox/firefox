/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Corrections to the SVG that Mermaid draws, applied whenever a diagram is
// drawn or redrawn.

// Mermaid sets a sequence diagram's height attribute to the height of its
// layout, without the room its viewBox adds for a title and, in the neo look,
// for the actor boxes. The browser then scales the whole drawing down to fit
// that height.
// TODO(bug 2088675): Remove once the Mermaid version in docs/conf.py has the
// fix for https://github.com/mermaid-js/mermaid/issues/8679.
const SEQUENCE = 'svg[aria-roledescription="sequence"][height][viewBox]';

document.addEventListener("DOMContentLoaded", () => {
  new MutationObserver(() => {
    for (const svg of document.querySelectorAll(SEQUENCE)) {
      const { height } = svg.viewBox.baseVal;
      if (svg.getAttribute("height") != height) {
        svg.setAttribute("height", height);
      }
    }
  }).observe(document.body, { childList: true, subtree: true });
});
