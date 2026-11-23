/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-input-password.tagmap.d.ts" />

import { MozInputText } from "chrome://global/content/elements/moz-input-text.mjs";

/**
 * A password input custom element.
 *
 * @tagname moz-input-password
 */
export class MozInputPassword extends MozInputText {
  inputTemplate() {
    return super.inputTemplate({ type: "password" });
  }
}
customElements.define("moz-input-password", MozInputPassword);
