/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  EventDispatcher: "resource://gre/modules/Messaging.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
});

/**
 * Monitors the sign-in state on Android and triggers service state updates.
 *
 * Unlike the FxA desktop counterpart, this watcher does not observe UIState
 * directly. Instead, it receives auth state changes from the Android layer via
 * the EventDispatcher ("GeckoView:IPProtection:AuthStateChanged"), and exposes
 * notifySignInStateChanged() for the mobile bridge to call directly if needed.
 */
class IPPAndroidSignInWatcherSingleton extends EventTarget {
  #signedIn = false;
  #listener = null;

  get isSignedIn() {
    return this.#signedIn;
  }

  init() {
    this.#listener = {
      onEvent: (_event, data, callback) => {
        try {
          this.notifySignInStateChanged(data.isSignedIn);
          callback?.onSuccess();
        } catch (error) {
          callback?.onError(error?.message ?? String(error));
        }
      },
    };
    lazy.EventDispatcher.instance.registerListener(this.#listener, [
      "GeckoView:IPProtection:AuthStateChanged",
    ]);
  }

  async initOnStartupCompleted() {}

  uninit() {
    if (this.#listener) {
      lazy.EventDispatcher.instance.unregisterListener(this.#listener, [
        "GeckoView:IPProtection:AuthStateChanged",
      ]);
      this.#listener = null;
    }
    this.#signedIn = false;
  }

  /**
   * Called by the Android layer when the FxA sign-in state changes.
   *
   * @param {boolean} isSignedIn
   */
  notifySignInStateChanged(isSignedIn) {
    if (isSignedIn === this.#signedIn) {
      return;
    }
    this.#signedIn = isSignedIn;
    lazy.IPProtectionService.updateState();
    this.dispatchEvent(
      new CustomEvent("IPPSignInWatcher:StateChanged", {
        bubbles: true,
        composed: true,
      })
    );
  }
}

const IPPAndroidSignInWatcher = new IPPAndroidSignInWatcherSingleton();

export { IPPAndroidSignInWatcher };
