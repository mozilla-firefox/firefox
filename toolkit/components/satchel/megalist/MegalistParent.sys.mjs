/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { MegalistViewModel } from "resource://gre/modules/megalist/MegalistViewModel.sys.mjs";

const lazy = {};
ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "MegalistParent",
    maxLogLevel: Services.prefs.getBoolPref(
      "browser.contextual-password-manager.log",
      false
    )
      ? "Debug"
      : "Warn",
  });
});

/**
 * MegalistParent integrates MegalistViewModel into Parent/Child model.
 */
export class MegalistParent extends JSWindowActorParent {
  #viewModel;

  actorCreated() {
    this.#viewModel = new MegalistViewModel((...args) =>
      this.sendAsyncMessage(...args)
    );
  }

  didDestroy() {
    this.#viewModel.willDestroy();
    this.#viewModel = null;
  }

  receiveMessage(message) {
    if (
      !this.manager.isInProcess ||
      this.manager.documentURI?.spec !==
        "chrome://global/content/megalist/megalist.html"
    ) {
      lazy.logConsole.debug(
        "MegalistParent: received message from the wrong content process type."
      );
      return null;
    }

    return this.#viewModel?.handleViewMessage(message);
  }

  authExpirationTime() {
    return this.#viewModel.authExpirationTime;
  }
}
