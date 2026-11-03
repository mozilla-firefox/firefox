/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GeckoViewUtils } from "resource://gre/modules/GeckoViewUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  GeckoViewPrompter:
    "moz-src:///mobile/shared/components/geckoview/GeckoViewPrompter.sys.mjs",
});

const { debug, warn } = GeckoViewUtils.initLogging("ColorPickerDelegate");

export class ColorPickerDelegate {
  // TODO(bug 1805397): Implement default colors
  init(aBrowsingContext, aTitle, aInitialColor, aDefaultColors) {
    this._browsingContext = aBrowsingContext;
    this._prompt = new lazy.GeckoViewPrompter(aBrowsingContext);
    this._msg = {
      type: "color",
      title: aTitle,
      value: aInitialColor,
      predefinedValues: aDefaultColors,
    };
  }

  open(aColorPickerShownCallback) {
    if (!this._browsingContext.canOpenModalPicker) {
      // Color pickers are not allowed to open, so we respond to the callback
      // with returnCancel.
      aColorPickerShownCallback.done("");
      return;
    }

    this._prompt.asyncShowPrompt(this._msg, result => {
      // OK: result
      // Cancel: !result
      aColorPickerShownCallback.done((result && result.color) || "");
    });
  }
}

ColorPickerDelegate.prototype.QueryInterface = ChromeUtils.generateQI([
  "nsIColorPicker",
]);
