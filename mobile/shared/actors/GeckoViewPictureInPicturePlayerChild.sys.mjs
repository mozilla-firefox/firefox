/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { PictureInPictureChild } from "moz-src:///toolkit/actors/PictureInPictureChild.sys.mjs";

// The player side of picture-in-picture is similar to desktop's: clone the source video into this
// document, mirror its styling, and stop cloning when the document goes away. It differs only in
// needing a viewport tag, since GeckoView enables meta viewport and desktop doesn't.
export class GeckoViewPictureInPicturePlayerChild extends PictureInPictureChild {
  setupPlayer(...args) {
    // With dom.meta-viewport.enabled on, as in GeckoView, Document::GetViewportInfo lays out a
    // document without a viewport tag at browser.viewport.desktopWidth (980px), wider than the
    // picture-in-picture window can zoom out to fit. Desktop has the pref off and lays out at the
    // window size, so it doesn't need this.
    if (!this.document.querySelector("meta[name=viewport]")) {
      const meta = this.document.createElement("meta");
      meta.name = "viewport";
      meta.content = "width=device-width, initial-scale=1";
      this.document.head.appendChild(meta);
    }
    return super.setupPlayer(...args);
  }
}
