/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { FxaAuthProviderImpl } from "moz-src:///browser/components/aiwindow/models/auth/FxaAuthProviderImpl.sys.mjs";
import { FxaSignedInWatcher } from "moz-src:///browser/components/aiwindow/models/auth/FxaSignedInWatcher.sys.mjs";
import { GuestAuthProviderImpl } from "moz-src:///browser/components/aiwindow/models/auth/GuestAuthProviderImpl.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const PREF_AUTH_IMPL = "browser.smartwindow.auth.impl";

const IMPLS = [FxaAuthProviderImpl, GuestAuthProviderImpl];

const lazy = XPCOMUtils.declareLazy({
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  log: () =>
    console.createInstance({
      prefix: "AuthImplSelector",
      maxLogLevel: Services.prefs.getBoolPref("browser.smartwindow.log", false)
        ? "Debug"
        : "Warn",
    }),
});

/**
 * Chooses and owns the AuthProviderImpl used by AuthProvider, based on the
 * auth impl pref and the Mozilla account state. The impl is chosen on first
 * use, and the FxA choice is persisted in the auth impl pref.
 *
 * If a Nimbus enrollment sets the smartWindow authImpl variable, its value is
 * copied to the auth impl pref when the pref has a different value.
 * Unenrolling does not reset the pref; it goes back to FxA when the server
 * rejects the guest token or when the user signs in.
 *
 * @fires AuthImplSelector#"AuthImplSelector:Logout"
 *  When the user signs out of their account.
 * @fires AuthImplSelector#"AuthImplSelector:ImplChanged"
 *  When an impl is set. detail: { providerName }
 * @fires AuthImplSelector#"AuthImplSelector:ImplSwitched"
 *  When the selection replaces an impl already in use with one of another
 *  type. detail: { from, to, reason }
 */
export class AuthImplSelector extends EventTarget {
  /** @type {AuthProviderImpl|null} */
  #impl = null;

  /**
   * Recorded as the reason key on auth_impl_switched. Pref changes not made
   * through #setAuthImplPref, for example in about:config, are recorded as
   * other.
   *
   * @type {"token_rejected"|"fxa_signed_in"|"nimbus"|"other"}
   */
  static #switchReason = "other";

  #onImplLogout = () =>
    this.dispatchEvent(new Event("AuthImplSelector:Logout"));

  #onFallbackNeeded = event => {
    if (Services.prefs.getStringPref(PREF_AUTH_IMPL, "") === "fxa") {
      lazy.log.error("Fallback requested, but FxA is already selected.");
      return;
    }
    AuthImplSelector.#setAuthImplPref("fxa", event.detail?.reason ?? "other");
  };

  #onSelectionInputChanged = () => {
    if (this.#impl) {
      this.#update();
    }
  };

  constructor() {
    super();
    Services.prefs.addObserver(PREF_AUTH_IMPL, this.#onSelectionInputChanged);
    FxaSignedInWatcher.addEventListener(
      "FxaSignedInWatcher:StateChanged",
      this.#onSelectionInputChanged
    );
  }

  /**
   * @returns {Promise<AuthProviderImpl>} The impl, chosen on first use
   */
  async getImpl() {
    if (!this.#impl) {
      await FxaSignedInWatcher.init();
      this.#update();
    }
    return this.#impl;
  }

  /**
   * @returns {string|null} The provider of the current impl, or null if none
   *   has been chosen yet. Does not trigger the selection.
   */
  get providerName() {
    return this.#impl?.providerName ?? null;
  }

  /**
   * Overrides the chosen impl until the selection changes.
   *
   * @param {AuthProviderImpl} impl
   */
  setImpl(impl) {
    this.#impl?.removeEventListener(
      "AuthProviderImpl:Logout",
      this.#onImplLogout
    );
    this.#impl?.removeEventListener(
      "AuthProviderImpl:FallbackNeeded",
      this.#onFallbackNeeded
    );
    this.#impl?.uninit();
    this.#impl = impl;
    this.dispatchEvent(
      new CustomEvent("AuthImplSelector:ImplChanged", {
        detail: { providerName: impl.providerName },
      })
    );
    this.#impl.addEventListener("AuthProviderImpl:Logout", this.#onImplLogout);
    this.#impl.addEventListener(
      "AuthProviderImpl:FallbackNeeded",
      this.#onFallbackNeeded
    );
  }

  /**
   * Writes the authImpl value of the enrollment to the auth impl pref.
   * Called by AIWindow on updates of the smartWindow feature.
   */
  static applyNimbusAuthImpl() {
    const authImpl =
      lazy.NimbusFeatures.smartWindow.getVariable("authImpl") ?? null;
    if (
      authImpl !== null &&
      authImpl !== Services.prefs.getStringPref(PREF_AUTH_IMPL)
    ) {
      AuthImplSelector.#setAuthImplPref(authImpl, "nimbus");
    }
  }

  #update() {
    const Impl = AuthImplSelector.#select();
    for (const Other of IMPLS) {
      if (Other !== Impl) {
        Other.cleanup();
      }
    }
    if (!(this.#impl instanceof Impl)) {
      const from = this.#impl?.providerName;
      this.setImpl(new Impl());
      if (from) {
        this.dispatchEvent(
          new CustomEvent("AuthImplSelector:ImplSwitched", {
            detail: {
              from,
              to: this.#impl.providerName,
              reason: AuthImplSelector.#switchReason,
            },
          })
        );
      }
    }
  }

  /**
   * Sets the auth impl pref, attributing the switch it causes to reason.
   *
   * @param {string} value
   * @param {"token_rejected"|"fxa_signed_in"|"nimbus"|"other"} reason
   */
  static #setAuthImplPref(value, reason) {
    AuthImplSelector.#switchReason = reason;
    try {
      Services.prefs.setStringPref(PREF_AUTH_IMPL, value);
    } finally {
      AuthImplSelector.#switchReason = "other";
    }
  }

  /**
   * @returns {typeof FxaAuthProviderImpl|typeof GuestAuthProviderImpl}
   */
  static #select() {
    const value = Services.prefs.getStringPref(PREF_AUTH_IMPL, "");
    switch (value) {
      case "guest":
        return GuestAuthProviderImpl;
      case "fxa":
        return FxaAuthProviderImpl;
      default:
        lazy.log.warn(
          `Unknown ${PREF_AUTH_IMPL} value "${value}", choosing as if unset.`
        );
      // falls through
      case "":
        if (!FxaSignedInWatcher.signedIn) {
          return GuestAuthProviderImpl;
        }
        AuthImplSelector.#setAuthImplPref("fxa", "fxa_signed_in");
        return FxaAuthProviderImpl;
    }
  }
}
