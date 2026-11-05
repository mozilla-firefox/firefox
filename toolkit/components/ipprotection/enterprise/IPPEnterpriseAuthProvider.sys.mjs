/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import {
  AUTH_ERRORS,
  IPPAuthProvider,
} from "moz-src:///toolkit/components/ipprotection/IPPAuthProvider.sys.mjs";
import {
  ProxyPass,
  ProxyUsage,
} from "moz-src:///toolkit/components/ipprotection/GuardianTypes.sys.mjs";

/**
 * Type Imports
 *
 * @typedef {import("../GuardianTypes.sys.mjs").TokenHandle} TokenHandle
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () =>
  console.createInstance({
    logModule: "IPP_EnterpriseAuthProvider",
    maxLogLevel: Services.prefs.getBoolPref("browser.ipProtection.log", false)
      ? "Debug"
      : "Warn",
  })
);

/**
 * Enterprise / Access-Connector implementation of IPPAuthProvider.
 *
 * The enterprise build always treats the user as signed-in and entitled. The
 * OAuth token comes from `Services.felt`, registered by
 * `browser/extensions/felt/api.js`. fetchProxyPass hits the access-connector
 * endpoint configured by `browser.ipProtection.guardian.endpoint` (same pref
 * and route as Guardian, but a different backend that does not emit
 * `X-Quota-*` headers — hence usage is canned, not parsed).
 */
class IPPEnterpriseAuthProviderSingleton extends IPPAuthProvider {
  constructor() {
    super();
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "accessConnectorEndpoint",
      "browser.ipProtection.guardian.endpoint",
      "https://vpn.mozilla.com"
    );
  }

  /**
   * @param {AbortSignal} [abortSignal]
   * @returns {Promise<TokenHandle>}
   */
  // eslint-disable-next-line require-await
  async getToken(abortSignal = null) {
    abortSignal?.throwIfAborted();
    // Services.felt is registered by browser/extensions/felt/api.js, which is
    // only built into enterprise builds, hence the eslint exception.
    // eslint-disable-next-line mozilla/valid-services
    const felt = Services.felt;
    if (!felt) {
      throw new Error(
        "IPPEnterpriseAuthProvider: Services.felt is not available"
      );
    }
    const token = felt.getAccessTokenIfValid();
    if (!token) {
      throw new Error(
        "IPPEnterpriseAuthProvider: Services.felt has no valid access token"
      );
    }
    return { token };
  }

  /**
   * @param {AbortSignal} [abortSignal]
   * @returns {Promise<ProxyUsage>}
   */
  // eslint-disable-next-line require-await
  async fetchProxyUsage(abortSignal = null) {
    abortSignal?.throwIfAborted();
    const reset = Temporal.Now.zonedDateTimeISO()
      .add(Temporal.Duration.from({ days: 30 }))
      .toString();
    return new ProxyUsage("1000000", "1000000", reset);
  }

  /**
   * Maps an access-connector HTTP status onto its meaning.
   *
   * @param {number} status
   * @returns {import("../IPPAuthProvider.sys.mjs").AuthError | null}
   * Null when the status carries no error.
   */
  static toError(status) {
    if (status === 200) {
      return null;
    }
    if (status === 401) {
      return AUTH_ERRORS.UNAUTHORIZED;
    }
    if (status >= 500 && status <= 599) {
      return AUTH_ERRORS.SERVER_ERROR;
    }
    return AUTH_ERRORS.UNEXPECTED_STATUS;
  }

  /**
   * Fetches a proxy pass from the access-connector backend.
   *
   * A status other than 200 always resolves with an error.
   *
   * @param {AbortSignal} [abortSignal]
   * @returns {Promise<{pass?: ProxyPass, status?: number, usage: null, error?: import("../IPPAuthProvider.sys.mjs").AuthError}>}
   */
  async fetchProxyPass(abortSignal = null) {
    const tokenHandle = await this.getToken(abortSignal);
    let response;
    try {
      response = await fetch(this.#tokenURL, {
        method: "GET",
        cache: "no-cache",
        headers: {
          Authorization: `Bearer ${tokenHandle.token}`,
          "Content-Type": "application/json",
        },
        signal: abortSignal,
      });
    } catch (error) {
      abortSignal?.throwIfAborted();
      lazy.logConsole.error("Proxy pass fetch failed:", error);
      return { error: AUTH_ERRORS.NETWORK_ERROR, usage: null };
    }
    const status = response.status;
    if (status === 401) {
      await tokenHandle.onTokenRejected?.();
    }
    const statusError = IPPEnterpriseAuthProviderSingleton.toError(status);
    if (statusError) {
      return { status, error: statusError, usage: null };
    }
    try {
      const pass = await ProxyPass.fromResponse(response);
      if (!pass) {
        return { status, error: AUTH_ERRORS.INVALID_RESPONSE, usage: null };
      }
      return { pass, status, usage: null };
    } catch (error) {
      lazy.logConsole.error("Error parsing pass:", error);
      return { status, error: AUTH_ERRORS.PARSE_ERROR, usage: null };
    }
  }

  get #tokenURL() {
    const url = new URL(this.accessConnectorEndpoint);
    url.pathname = "/api/v1/fpn/token";
    return url;
  }

  // eslint-disable-next-line require-await
  async enroll() {
    this.dispatchEvent(
      new CustomEvent("IPPAuthProvider:StateChanged", {
        bubbles: true,
        composed: true,
      })
    );
    return { isEnrolledAndEntitled: true };
  }

  async aboutToStart() {
    return null;
  }

  init() {}

  // eslint-disable-next-line require-await
  async initOnStartupCompleted() {
    this.dispatchEvent(
      new CustomEvent("IPPAuthProvider:StateChanged", {
        bubbles: true,
        composed: true,
      })
    );
    lazy.IPProtectionService.updateState();
  }

  uninit() {}

  get helpers() {
    return [this];
  }

  get isReady() {
    return true;
  }

  get hasUpgraded() {
    return true;
  }

  get maxBytes() {
    return BigInt(1000000);
  }

  get isEnrolling() {
    return false;
  }

  // eslint-disable-next-line require-await
  async checkForUpgrade() {}

  get excludedUrlPrefs() {
    return [];
  }
}

const IPPEnterpriseAuthProvider = new IPPEnterpriseAuthProviderSingleton();

export { IPPEnterpriseAuthProvider, IPPEnterpriseAuthProviderSingleton };
