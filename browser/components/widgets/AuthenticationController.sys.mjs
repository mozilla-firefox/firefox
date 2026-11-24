/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  SpecialMessageActions:
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs",
  getFxAccountsSingleton: "resource://gre/modules/FxAccounts.sys.mjs",
  UIState: "resource://services-sync/UIState.sys.mjs",
});

/**
 * Tracks whether the user is signed in to FxA and updates the host when
 * that changes. It also starts the sign-in flow and tracks it while it runs.
 */
export class AuthenticationController {
  /** @type {?boolean} whether the user is signed in, null until the status is known */
  authenticated = null;

  /** @type {boolean} whether a sign-in flow is running */
  pending = false;

  /**
   * @param {ReactiveControllerHost} host - the element to update
   */
  constructor(host) {
    this.host = host;
    host.addController(this);
  }

  get isStatusPending() {
    return this.authenticated === null;
  }

  hostConnected() {
    Services.obs.addObserver(this, lazy.UIState.ON_UPDATE);
    this.observe();
    if (this.isStatusPending) {
      this.#loadStatus();
    }
  }

  async #loadStatus() {
    let signedIn = false;
    try {
      let user = await lazy.getFxAccountsSingleton().getSignedInUser();
      signedIn = !!user?.verified;
    } catch (e) {
      console.error("Error reading the sign-in status:", e);
    }
    if (this.isStatusPending) {
      this.#setAuthenticated(signedIn);
    }
  }

  hostDisconnected() {
    Services.obs.removeObserver(this, lazy.UIState.ON_UPDATE);
  }

  observe() {
    let { status } = lazy.UIState.get();
    if (this.isStatusPending && status == lazy.UIState.STATUS_NOT_CONFIGURED) {
      // UIState reports not_configured both before it has loaded the account
      // and when signed out, so wait for the account lookup instead.
      return;
    }
    this.#setAuthenticated(status == lazy.UIState.STATUS_SIGNED_IN);
  }

  #setAuthenticated(authenticated) {
    if (authenticated !== this.authenticated) {
      this.authenticated = authenticated;
      this.host.requestUpdate();
    }
  }

  async signIn() {
    if (this.pending) {
      return;
    }
    this.pending = true;
    this.host.requestUpdate();
    try {
      await lazy.SpecialMessageActions.fxaSignInFlow(
        { entrypoint: "widget_studio" },
        this.host.topWindow.gBrowser.selectedBrowser
      );
    } catch (e) {
      console.error("Error launching sign-in flow:", e);
    } finally {
      this.pending = false;
      this.host.requestUpdate();
    }
  }
}
