/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "fxAccounts", () =>
  ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton()
);
ChromeUtils.defineESModuleGetters(lazy, {
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  UIState: "resource://services-sync/UIState.sys.mjs",
});

/**
 * This class monitors the Sign-In state and triggers the update of the service
 * if needed.
 */
class IPPSignInWatcherSingleton extends EventTarget {
  #signedIn = false;

  get isSignedIn() {
    return this.#signedIn;
  }

  set isSignedIn(signedIn) {
    this.#signedIn = signedIn;
  }

  init() {
    // Get signed in state
    this.#refreshFromFxA();
  }

  /**
   * Re-reads the sign-in state from FxA, for callers that found it stale.
   *
   * @returns {Promise<void>}
   */
  refresh() {
    return this.#refreshFromFxA();
  }

  async #refreshFromFxA() {
    let signedIn = false;
    try {
      const userData = await lazy.fxAccounts.getSignedInUser();
      signedIn = !!userData?.verified;
    } catch (_) {
      signedIn = false;
    }
    this.#setSignedIn(signedIn);
  }

  #setSignedIn(signedIn) {
    if (signedIn === this.#signedIn) {
      return;
    }
    this.#signedIn = signedIn;
    lazy.IPProtectionService.updateState();
    this.dispatchEvent(
      new CustomEvent("IPPSignInWatcher:StateChanged", {
        bubbles: true,
        composed: true,
      })
    );
  }

  /**
   * Adds an observer for the FxA sign-in state, only when the browser is fully started.
   */
  async initOnStartupCompleted() {
    this.fxaObserver = {
      QueryInterface: ChromeUtils.generateQI([
        Ci.nsIObserver,
        Ci.nsISupportsWeakReference,
      ]),

      observe() {
        let { status } = lazy.UIState.get();
        let signedIn = status == lazy.UIState.STATUS_SIGNED_IN;
        IPPSignInWatcher.#setSignedIn(signedIn);
      },
    };

    Services.obs.addObserver(this.fxaObserver, lazy.UIState.ON_UPDATE);
  }

  /**
   * Removes the FxA sign-in state observer
   */
  uninit() {
    if (this.fxaObserver) {
      Services.obs.removeObserver(this.fxaObserver, lazy.UIState.ON_UPDATE);
      this.fxaObserver = null;
    }
  }
}

const IPPSignInWatcher = new IPPSignInWatcherSingleton();

export { IPPSignInWatcher };
