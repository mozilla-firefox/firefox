/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { FxaAuthProviderImpl } from "moz-src:///browser/components/aiwindow/models/auth/FxaAuthProviderImpl.sys.mjs";
import { GuestAuthProviderImpl } from "moz-src:///browser/components/aiwindow/models/auth/GuestAuthProviderImpl.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const PREF_TOS_CONSENT_TIME = "browser.smartwindow.tos.consentTime";
const PREF_AUTH_IMPL = "browser.smartwindow.auth.impl";

const lazy = XPCOMUtils.declareLazy({
  hasAIWindowToSConsent: {
    pref: PREF_TOS_CONSENT_TIME,
    default: 0,
  },
  log: () =>
    console.createInstance({
      prefix: "AuthProvider",
      maxLogLevel: Services.prefs.getBoolPref("browser.smartwindow.log", false)
        ? "Debug"
        : "Warn",
    }),
});

/**
 * Handles authentication and Terms of Service consent for Smart Window.
 * Account-specific operations are delegated to an AuthProviderImpl.
 *
 * @fires AuthProviderSingleton#"AuthProvider:Logout"
 *  When the user signs out of their account.
 */
class AuthProviderSingleton extends EventTarget {
  /** @type {AuthProviderImpl} */
  #impl;

  #onLogout = () => this.dispatchEvent(new Event("AuthProvider:Logout"));

  #onFallbackNeeded = () => {
    if (Services.prefs.getStringPref(PREF_AUTH_IMPL, "") !== "guest") {
      lazy.log.error(
        "Fallback requested, but the guest provider is not enabled. Not switching to FxA."
      );
      return;
    }
    Services.prefs.setStringPref(PREF_AUTH_IMPL, "fxa");
  };

  #onAuthImplChanged = () => this.setImpl(AuthProviderSingleton.#createImpl());

  constructor() {
    super();
    this.setImpl(AuthProviderSingleton.#createImpl());
    Services.prefs.addObserver(PREF_AUTH_IMPL, this.#onAuthImplChanged);
  }

  /**
   * @returns {AuthProviderImpl} The impl selected by the auth impl pref
   */
  static #createImpl() {
    const value = Services.prefs.getStringPref(PREF_AUTH_IMPL, "");
    if (value === "guest") {
      return new GuestAuthProviderImpl();
    }
    if (value !== "" && value !== "fxa") {
      lazy.log.warn(`Unknown ${PREF_AUTH_IMPL} value "${value}", using FxA.`);
    }
    return new FxaAuthProviderImpl();
  }

  /**
   * Sets the backend used for account-specific operations.
   *
   * @param {AuthProviderImpl} impl
   */
  setImpl(impl) {
    this.#impl?.removeEventListener("AuthProviderImpl:Logout", this.#onLogout);
    this.#impl?.removeEventListener(
      "AuthProviderImpl:FallbackNeeded",
      this.#onFallbackNeeded
    );
    this.#impl = impl;
    this.#impl.addEventListener("AuthProviderImpl:Logout", this.#onLogout);
    this.#impl.addEventListener(
      "AuthProviderImpl:FallbackNeeded",
      this.#onFallbackNeeded
    );
  }

  get hasToSConsent() {
    return !!lazy.hasAIWindowToSConsent;
  }

  recordToSConsent() {
    Services.prefs.setIntPref(
      PREF_TOS_CONSENT_TIME,
      Math.floor(Date.now() / 1000)
    );
  }

  clearToSConsent() {
    Services.prefs.clearUserPref(PREF_TOS_CONSENT_TIME);
  }

  /**
   * Retrieves the account token
   *
   * @returns {Promise<string|null>}   The account token (string) or null
   */
  getToken() {
    return this.#impl.getToken();
  }

  /**
   * Drops a token the server rejected, so the next getToken call fetches a
   * fresh one.
   *
   * @param {string|null} token
   */
  async invalidateToken(token) {
    if (!token) {
      return;
    }
    await this.#impl.invalidateToken(token);
  }

  /**
   * Whether the user is signed in to their account.
   *
   * @returns {Promise<boolean>}
   */
  isSignedIn() {
    return this.#impl.isSignedIn();
  }

  /**
   * Runs the sign-in flow, recording a signin_flow_started /
   * signin_flow_completed pair around it.
   *
   * @param {Browser} browser
   * @param {object} options
   * @param {string} options.reason Why access was denied, recorded on
   *   signin_flow_started
   * @returns {Promise<boolean>} Whether the user signed in successfully
   */
  async runSignInFlow(browser, { reason }) {
    Glean.smartWindow.signinFlowStarted.record({ reason });

    let outcome = "abandoned";
    try {
      // Checked here as well as inside the sign-in flow so that a blocked
      // flow, where the sign-in page is never shown, is distinguishable from
      // one the user abandoned. Both make signIn return false.
      if (!(await this.#impl.canSignIn())) {
        outcome = "blocked";
        return false;
      }

      const didSignIn = await this.#impl.signIn(browser);
      if (didSignIn) {
        outcome = "completed";
      }
      return didSignIn;
    } catch (error) {
      outcome = "error";
      lazy.log.error("Error prompting sign-in:", error);
      throw error;
    } finally {
      Glean.smartWindow.signinFlowCompleted.record({ outcome });
    }
  }

  /**
   * Describes why Smart Window access was denied, which is what makes the
   * sign-in flow necessary. Recorded as the reason key on signin_flow_started.
   *
   * @param {boolean} signedIn Whether the user is signed in to their account
   * @returns {string} One of signed_out, no_consent, both, or none
   */
  #denialReason(signedIn) {
    if (this.hasToSConsent) {
      return signedIn ? "none" : "signed_out";
    }
    return signedIn ? "no_consent" : "both";
  }

  /**
   * Whether the user can use Smart Window without going through the sign-in
   * flow: they have consented to the Terms of Service and are signed in.
   *
   * @returns {Promise<boolean>}
   */
  async hasAccess() {
    return this.hasToSConsent && (await this.isSignedIn());
  }

  /**
   * Runs the sign-in flow unless the user already has access.
   *
   * @param {Browser} browser
   * @returns {Promise<boolean>} Whether the user has access afterwards
   */
  async maybeSignIn(browser) {
    if (await this.hasAccess()) {
      return true;
    }
    return this.promptSignIn(browser);
  }

  /**
   * Runs the sign-in flow regardless of the current sign-in state, and records
   * Terms of Service consent once the user signs in.
   *
   * @param {Browser} browser
   * @returns {Promise<boolean>} Whether the user signed in successfully
   */
  async promptSignIn(browser) {
    const signedIn = await this.isSignedIn();

    const didSignIn = await this.runSignInFlow(browser, {
      reason: this.#denialReason(signedIn),
    });
    if (!didSignIn) {
      lazy.log.error("User did not sign in successfully.");
      return false;
    }
    this.recordToSConsent();
    return true;
  }
}

export const AuthProvider = new AuthProviderSingleton();
