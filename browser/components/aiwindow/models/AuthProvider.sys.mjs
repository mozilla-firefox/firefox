/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { AuthImplSelector } from "moz-src:///browser/components/aiwindow/models/auth/AuthImplSelector.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

// "tos" is legacy naming: the consent is for the Firefox Terms of Use (ToU).
const PREF_TOU_CONSENT_TIME = "browser.smartwindow.tos.consentTime";

const lazy = XPCOMUtils.declareLazy({
  hasAIWindowToUConsent: {
    pref: PREF_TOU_CONSENT_TIME,
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
 * Handles authentication and Terms of Use consent for Smart Window.
 * Account-specific operations are delegated to an AuthProviderImpl.
 *
 * @fires AuthProviderSingleton#"AuthProvider:Logout"
 *  When the user signs out of their account.
 * @fires AuthProviderSingleton#"AuthProvider:ImplChanged"
 *  When an impl is set. detail: { providerName }
 * @fires AuthProviderSingleton#"AuthProvider:ImplSwitched"
 *  When the impl in use is replaced by one of another type.
 *  detail: { from, to, reason }
 * @fires AuthProviderSingleton#"AuthProvider:SignInFlowStarted"
 *  When the sign-in flow starts. detail: { reason }
 * @fires AuthProviderSingleton#"AuthProvider:SignInFlowCompleted"
 *  When the sign-in flow ends. detail: { outcome }
 */
class AuthProviderSingleton extends EventTarget {
  /** @type {AuthImplSelector} */
  #selector;

  constructor() {
    super();
    this.#selector = new AuthImplSelector();
    this.#selector.addEventListener("AuthImplSelector:Logout", () =>
      this.dispatchEvent(new Event("AuthProvider:Logout"))
    );
    for (const type of ["ImplChanged", "ImplSwitched"]) {
      this.#selector.addEventListener(`AuthImplSelector:${type}`, event =>
        this.#dispatch(type, event.detail)
      );
    }
  }

  /**
   * @param {string} type Event type, without the AuthProvider: prefix
   * @param {object} detail
   */
  #dispatch(type, detail) {
    this.dispatchEvent(new CustomEvent(`AuthProvider:${type}`, { detail }));
  }

  /**
   * @returns {string|null} The provider name of the active backend, or null
   *   if none has been chosen yet. Unlike getProviderName, does not trigger
   *   the selection.
   */
  get currentProviderName() {
    return this.#selector.providerName;
  }

  /**
   * Sets the backend used for account-specific operations.
   *
   * @param {AuthProviderImpl} impl
   */
  setImpl(impl) {
    this.#selector.setImpl(impl);
  }

  get hasToUConsent() {
    return !!lazy.hasAIWindowToUConsent;
  }

  recordToUConsent() {
    Services.prefs.setIntPref(
      PREF_TOU_CONSENT_TIME,
      Math.floor(Date.now() / 1000)
    );
  }

  clearToUConsent() {
    Services.prefs.clearUserPref(PREF_TOU_CONSENT_TIME);
  }

  /**
   * Retrieves the account token
   *
   * @returns {Promise<string|null>}   The account token (string) or null
   */
  async getToken() {
    const impl = await this.#selector.getImpl();
    return impl.getToken();
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
    const impl = await this.#selector.getImpl();
    await impl.invalidateToken(token);
  }

  /**
   * @returns {Promise<string>} The provider name of the active backend
   */
  async getProviderName() {
    const impl = await this.#selector.getImpl();
    return impl.providerName;
  }

  /**
   * Whether the user is signed in to their account.
   *
   * @returns {Promise<boolean>}
   */
  async isSignedIn() {
    const impl = await this.#selector.getImpl();
    return impl.isSignedIn();
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
    this.#dispatch("SignInFlowStarted", { reason });

    let outcome = "abandoned";
    try {
      // Checked here as well as inside the sign-in flow so that a blocked
      // flow, where the sign-in page is never shown, is distinguishable from
      // one the user abandoned. Both make signIn return false.
      const impl = await this.#selector.getImpl();
      if (!(await impl.canSignIn())) {
        outcome = "blocked";
        return false;
      }

      const didSignIn = await impl.signIn(browser);
      if (didSignIn) {
        outcome = "completed";
      }
      return didSignIn;
    } catch (error) {
      outcome = "error";
      lazy.log.error("Error prompting sign-in:", error);
      throw error;
    } finally {
      this.#dispatch("SignInFlowCompleted", { outcome });
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
    if (this.hasToUConsent) {
      return signedIn ? "none" : "signed_out";
    }
    return signedIn ? "no_consent" : "both";
  }

  /**
   * Whether the user can use Smart Window without going through the sign-in
   * flow: they have consented to the Terms of Use and are signed in.
   *
   * @returns {Promise<boolean>}
   */
  async hasAccess() {
    return this.hasToUConsent && (await this.isSignedIn());
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
   * Terms of Use consent once the user signs in.
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
    this.recordToUConsent();
    return true;
  }
}

export const AuthProvider = new AuthProviderSingleton();
