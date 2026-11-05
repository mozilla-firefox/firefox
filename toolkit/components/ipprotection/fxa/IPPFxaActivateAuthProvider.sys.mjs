/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  AUTH_ERRORS,
  IPPAuthProvider,
} from "moz-src:///toolkit/components/ipprotection/IPPAuthProvider.sys.mjs";
import { GuardianClient } from "moz-src:///toolkit/components/ipprotection/fxa/GuardianClient.sys.mjs";

/**
 * Type Imports
 *
 * @typedef {import("../GuardianTypes.sys.mjs").TokenHandle} TokenHandle
 */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "fxAccounts", () =>
  ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton()
);
ChromeUtils.defineESModuleGetters(lazy, {
  IPPSignInWatcher:
    "moz-src:///toolkit/components/ipprotection/fxa/IPPSignInWatcher.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  IPPStartupCache:
    "moz-src:///toolkit/components/ipprotection/IPPStartupCache.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () =>
  console.createInstance({
    logModule: "IPP_FxaActivateAuthProvider",
    maxLogLevel: Services.prefs.getBoolPref("browser.ipProtection.log", false)
      ? "Debug"
      : "Warn",
  })
);

/**
 * FxA implementation of IPPAuthProvider that uses the direct token activation
 * flow by calling Guardian's POST /api/v1/fpn/activate endpoint with the FxA
 * Bearer token.
 *
 * Provides shared OAuth token retrieval, Guardian proxy methods, sign-in watcher
 * access, and entitlement lifecycle management.
 */
class IPPFxaActivateAuthProviderSingleton extends IPPAuthProvider {
  #entitlement = null;
  #signInWatcher = null;
  #guardian = new GuardianClient();
  #isEnrolling = false;

  /**
   * @param {object} [signInWatcher] - Custom sign-in watcher. Defaults to IPPSignInWatcher.
   */
  constructor(signInWatcher = null) {
    super();
    this.#signInWatcher = signInWatcher;
    this.handleEvent = this.#handleEvent.bind(this);
  }

  get guardian() {
    return this.#guardian;
  }

  get signInWatcher() {
    return this.#signInWatcher ?? lazy.IPPSignInWatcher;
  }

  get entitlement() {
    return this.#entitlement;
  }

  get helpers() {
    return [this, this.signInWatcher];
  }

  /**
   * Updates the stored entitlement and persists it to the startup cache.
   *
   * @param {object|null} entitlement
   */
  _setEntitlement(entitlement) {
    this.#entitlement = entitlement;
    lazy.IPPStartupCache.storeEntitlement(entitlement);
  }

  init() {
    this.#entitlement = lazy.IPPStartupCache.entitlement;
    this.signInWatcher.addEventListener(
      "IPPSignInWatcher:StateChanged",
      this.handleEvent
    );
  }

  initOnStartupCompleted() {
    if (!this.signInWatcher.isSignedIn) {
      return;
    }
    this.updateEntitlement();
  }

  uninit() {
    this.signInWatcher.removeEventListener(
      "IPPSignInWatcher:StateChanged",
      this.handleEvent
    );
    this.#entitlement = null;
  }

  #handleEvent() {
    if (!this.signInWatcher.isSignedIn) {
      this._setEntitlement(null);
      lazy.IPProtectionService.updateState();
      return;
    }
    // Force rechecking the entitlement when sign-in state changes.
    this.updateEntitlement();
  }

  async updateEntitlement() {
    const { entitlement, error } = await this.getEntitlement();
    if (!error) {
      this._setEntitlement(entitlement ?? null);
    }
    lazy.IPProtectionService.updateState();
  }

  async getEntitlement() {
    try {
      const tokenHandle = await this.getToken();
      const { status, entitlement, error } =
        await this.guardian.fetchUserInfo(tokenHandle);
      if (error || status != 200) {
        return { error: error || `Status: ${status}` };
      }
      return { entitlement: entitlement ?? null };
    } catch (error) {
      return { error: error?.message ?? error };
    }
  }

  get hasUpgraded() {
    return this.entitlement?.subscribed ?? false;
  }

  get maxBytes() {
    return this.entitlement?.maxBytes ?? null;
  }

  get limitedBandwidth() {
    return this.entitlement?.limitedBandwidth ?? true;
  }

  get isReady() {
    return this.signInWatcher.isSignedIn && !!this.entitlement;
  }

  get isEnrolling() {
    return this.#isEnrolling;
  }

  async aboutToStart() {
    return null;
  }

  async checkForUpgrade() {
    const tokenHandle = await this.getToken();
    const { entitlement } = await this.guardian.fetchUserInfo(tokenHandle);
    if (entitlement) {
      this._setEntitlement(entitlement);
      lazy.IPProtectionService.updateState();
      this.dispatchEvent(
        new CustomEvent("IPPAuthProvider:StateChanged", {
          bubbles: true,
          composed: true,
        })
      );
    }
  }

  async enroll() {
    if (!this.signInWatcher.isSignedIn) {
      // The token request below fails if the account is genuinely signed out,
      // so a stale watcher is reported rather than treated as fatal.
      lazy.logConsole.warn(
        "enroll() called while the sign-in watcher reports signed out."
      );
      Glean.ipprotection.error.record({ source: "AuthProvider:SignInDesync" });
    }
    this.#isEnrolling = true;
    this.dispatchEvent(new CustomEvent("IPPAuthProvider:StateChanged"));
    try {
      const tokenHandle = await this.getToken();
      const { ok, entitlement, error } =
        await this.guardian.activate(tokenHandle);
      if (!ok) {
        return { isEnrolledAndEntitled: false, error };
      }
      this._setEntitlement(entitlement ?? null);
      return { isEnrolledAndEntitled: true, error: null };
    } catch (error) {
      lazy.logConsole.error("Enrollment failed:", error);
      return {
        isEnrolledAndEntitled: false,
        error: AUTH_ERRORS.ENROLLMENT_FAILED,
      };
    } finally {
      this.#isEnrolling = false;
      lazy.IPProtectionService.updateState();
      this.dispatchEvent(new CustomEvent("IPPAuthProvider:StateChanged"));
    }
  }

  /**
   * Retrieves an FxA OAuth token and returns a handle that revokes it if
   * Guardian rejects it.
   *
   * @param {AbortSignal} [abortSignal]
   * @returns {Promise<TokenHandle|null>}
   */
  async getToken(abortSignal = null) {
    let tasks = [lazy.fxAccounts.getOAuthToken({ service: "vpn" })];
    if (abortSignal) {
      abortSignal.throwIfAborted();
      tasks.push(
        new Promise((_, rej) => {
          abortSignal?.addEventListener(
            "abort",
            () => rej(abortSignal.reason),
            { once: true }
          );
        })
      );
    }
    const token = await Promise.race(tasks);
    if (!token) {
      return null;
    }
    return {
      token,
      // Evicts our cache entry and revokes the token server side.
      onTokenRejected: () => lazy.fxAccounts.removeCachedOAuthToken({ token }),
    };
  }

  async fetchProxyPass(abortSignal = null) {
    const tokenHandle = await this.getToken(abortSignal);
    return await this.#guardian.fetchProxyPass(tokenHandle, abortSignal);
  }

  async fetchProxyUsage(abortSignal = null) {
    const tokenHandle = await this.getToken(abortSignal);
    return await this.#guardian.fetchProxyUsage(tokenHandle, abortSignal);
  }

  get excludedUrlPrefs() {
    return [
      "identity.fxaccounts.remote.profile.uri",
      "identity.fxaccounts.auth.uri",
    ];
  }
}

const IPPFxaActivateAuthProvider = new IPPFxaActivateAuthProviderSingleton();

export { IPPFxaActivateAuthProvider, IPPFxaActivateAuthProviderSingleton };
