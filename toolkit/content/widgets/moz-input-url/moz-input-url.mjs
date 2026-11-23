/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-input-url.tagmap.d.ts" />

import { MozInputText } from "chrome://global/content/elements/moz-input-text.mjs";

/**
 * A URL input custom element.
 *
 * @tagname moz-input-url
 */
export class MozInputUrl extends MozInputText {
  inputTemplate() {
    return super.inputTemplate({ type: "url" });
  }
}
customElements.define("moz-input-url", MozInputUrl);
