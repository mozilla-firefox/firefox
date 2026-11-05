/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  getFxAccountsSingleton: "resource://gre/modules/FxAccounts.sys.mjs",
  UIState: "resource://services-sync/UIState.sys.mjs",
  log: () =>
    console.createInstance({
      logModule: "FxaSignedInWatcher",
      maxLogLevel: Services.prefs.getBoolPref("browser.smartwindow.log", false)
        ? "Debug"
        : "Warn",
    }),
});

/**
 * Tracks whether a verified Mozilla account is signed in, so that the last
 * known state can be read synchronously.
 *
 * @fires FxaSignedInWatcherSingleton#"FxaSignedInWatcher:StateChanged"
 *  When the signed-in state changes.
 */
class FxaSignedInWatcherSingleton extends EventTarget {
  #signedIn = false;

  /** @type {Promise<void>|null} */
  #initPromise = null;

  #onUIStateUpdate = () =>
    this.#setSignedIn(
      lazy.UIState.get().status === lazy.UIState.STATUS_SIGNED_IN
    );

  /**
   * The last known state. Only meaningful once init has resolved.
   *
   * @returns {boolean}
   */
  get signedIn() {
    return this.#signedIn;
  }

  /**
   * Waits for init, then returns the signed-in state.
   *
   * @returns {Promise<boolean>} Whether a verified Mozilla account is signed in
   */
  async isFxaSignedIn() {
    await this.init();
    return this.signedIn;
  }

  /**
   * Reads the initial state and starts watching for changes.
   *
   * @returns {Promise<void>}
   */
  init() {
    this.#initPromise ??= (async () => {
      Services.obs.addObserver(this.#onUIStateUpdate, lazy.UIState.ON_UPDATE);
      try {
        const user = await lazy.getFxAccountsSingleton().getSignedInUser();
        this.#setSignedIn(!!user?.verified);
      } catch (error) {
        lazy.log.error("Error checking the account state:", error);
      }
    })();
    return this.#initPromise;
  }

  #setSignedIn(signedIn) {
    if (signedIn === this.#signedIn) {
      return;
    }
    this.#signedIn = signedIn;
    this.dispatchEvent(new Event("FxaSignedInWatcher:StateChanged"));
  }
}

export const FxaSignedInWatcher = new FxaSignedInWatcherSingleton();
