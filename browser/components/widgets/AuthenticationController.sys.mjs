/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  UIState: "resource://services-sync/UIState.sys.mjs",
});

/**
 * Tracks whether the user is signed in to FxA and updates the host when
 * that changes.
 */
export class AuthenticationController {
  /** @type {boolean} whether the user is signed in */
  authenticated = false;

  /**
   * @param {ReactiveControllerHost} host - the element to update
   */
  constructor(host) {
    this.host = host;
    host.addController(this);
  }

  hostConnected() {
    Services.obs.addObserver(this, lazy.UIState.ON_UPDATE);
    this.observe();
  }

  hostDisconnected() {
    Services.obs.removeObserver(this, lazy.UIState.ON_UPDATE);
  }

  observe() {
    let authenticated =
      lazy.UIState.get().status == lazy.UIState.STATUS_SIGNED_IN;
    if (authenticated != this.authenticated) {
      this.authenticated = authenticated;
      this.host.requestUpdate();
    }
  }
}
