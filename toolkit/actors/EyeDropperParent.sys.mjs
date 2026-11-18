/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { InputPickerParentCommon } from "./InputPickerParentCommon.sys.mjs";

/** @import {InputPickerCallbacks} from "./InputPickerParentCommon.sys.mjs" */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  EyeDropper: "moz-src:///toolkit/modules/EyeDropper.sys.mjs",
});

const DEBUG = false;
function debug(aStr) {
  if (DEBUG) {
    dump("-*- EyeDropperPicker: " + aStr + "\n");
  }
}

/**
 * Shows the eyedropper over the tab, in the browser window, as a picker for
 * InputPickerParentCommon. The page screenshot is taken and sampled in the
 * parent process, so that out-of-process iframes are included and only the
 * picked color is sent to the content process.
 */
class EyeDropperPicker {
  /** @type {EyeDropper} */
  #eyeDropper = null;
  /** @type {AbortController} */
  #abortController = null;

  /**
   * @param {XULBrowserElement} aBrowser The browser to show the eyedropper
   *        over. Throws if there is none, e.g. for an input in a top-level
   *        chrome window, which is not supported yet.
   */
  constructor(aBrowser) {
    if (!aBrowser) {
      throw new Error(
        "EyeDropper does not yet support being opened for chrome documents"
      );
    }

    this.#eyeDropper = new lazy.EyeDropper(aBrowser);
  }

  /**
   * @param {string} _aType
   * @param {DOMRect} _aRect
   * @param {object} _aDetail
   * @param {boolean} _aIsDarkBackground
   * @param {InputPickerCallbacks} aCallbacks
   */
  openPicker(_aType, _aRect, _aDetail, _aIsDarkBackground, aCallbacks) {
    debug("openPicker");
    const abortController = new AbortController();
    const signal = abortController.signal;
    this.#abortController = abortController;
    this.#eyeDropper
      .open({ signal })
      .then(color => {
        if (color) {
          aCallbacks.onValueChanged({
            rgb: [color.r / 255, color.g / 255, color.b / 255, 1],
          });
        }
      })
      .catch(console.error)
      .finally(() => {
        if (abortController.signal.aborted) {
          return;
        }
        this.#abortController = null;
        aCallbacks.onClosed();
      });
  }

  closePicker() {
    debug("closePicker");
    this.#abortController?.abort();
    this.#abortController = null;
  }
}

export class EyeDropperParent extends InputPickerParentCommon {
  constructor() {
    super("EyeDropper", false);
  }

  /**
   * A picker creator function called when showing a picker
   *
   * @returns A picker object
   */
  createPickerImpl() {
    return new EyeDropperPicker(this.browsingContext.top.embedderElement);
  }
}
