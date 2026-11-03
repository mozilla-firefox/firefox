/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GeckoViewUtils } from "resource://gre/modules/GeckoViewUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  GeckoViewPrompter:
    "moz-src:///mobile/shared/components/geckoview/GeckoViewPrompter.sys.mjs",
});

const { debug, warn } = GeckoViewUtils.initLogging("PromptCollection");

export class PromptCollection {
  confirmRepost(browsingContext) {
    const msg = {
      type: "repost",
    };
    const prompter = new lazy.GeckoViewPrompter(browsingContext);
    const result = prompter.showPrompt(msg);
    return !!result?.allow;
  }

  asyncBeforeUnloadCheck(browsingContext) {
    return new Promise(resolve => {
      const msg = {
        type: "beforeUnload",
      };
      const prompter = new lazy.GeckoViewPrompter(browsingContext);
      prompter.asyncShowPrompt(msg, resolve);
    }).then(result => !!result?.allow);
  }

  confirmFolderUpload(browsingContext, directoryName) {
    const msg = {
      type: "folderUpload",
      directoryName,
    };
    const prompter = new lazy.GeckoViewPrompter(browsingContext);
    const result = prompter.showPrompt(msg);
    return !!result?.allow;
  }
}

PromptCollection.prototype.QueryInterface = ChromeUtils.generateQI([
  "nsIPromptCollection",
]);
