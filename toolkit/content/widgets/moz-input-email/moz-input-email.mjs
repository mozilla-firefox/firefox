/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-input-email.tagmap.d.ts" />

import { MozInputText } from "chrome://global/content/elements/moz-input-text.mjs";

/**
 * An email input custom element.
 *
 * @tagname moz-input-email
 */
export class MozInputEmail extends MozInputText {
  inputTemplate() {
    return super.inputTemplate({ type: "email" });
  }
}
customElements.define("moz-input-email", MozInputEmail);
