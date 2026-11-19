/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  AUTH_ERRORS,
  IPPAuthProvider,
} from "moz-src:///toolkit/components/ipprotection/IPPAuthProvider.sys.mjs";
import {
  ProxyPass,
  ProxyUsage,
} from "moz-src:///toolkit/components/ipprotection/GuardianTypes.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  EventDispatcher: "resource://gre/modules/Messaging.sys.mjs",
  // GPI talks to the same Guardian service, so it shares its status semantics.
  GuardianClient:
    "moz-src:///toolkit/components/ipprotection/fxa/GuardianClient.sys.mjs",
  IPProtectionActivator:
    "moz-src:///toolkit/components/ipprotection/IPProtectionActivator.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

const AUTH_JWT_PREF = "browser.ipProtection.gpi.authJwt";
const AUTH_JWT_EXPIRES_AT_PREF = "browser.ipProtection.gpi.authJwtExpiresAt";
const AUTH_JWT_RENEW_AFTER_PREF = "browser.ipProtection.gpi.authJwtRenewAfter";
const GUARDIAN_ENDPOINT_PREF = "browser.ipProtection.guardian.endpoint";
const GUARDIAN_ENDPOINT_DEFAULT = "https://vpn.mozilla.com";

/**
 * Maps a Guardian error body to a reason category for telemetry.
 *
 * @param {string} bodyText
 * @returns {string}
 */
function categorizeEnrollmentReason(bodyText) {
  let serverReason = "";
  try {
    serverReason = JSON.parse(bodyText)?.reason ?? "";
  } catch {
    // non-JSON: return http_error_other.
  }
  switch (serverReason) {
    case "missing-package-name":
      return "missing_package_name";
    case "integrity-api-error":
      return "integrity_api_error";
    default:
      return "http_error_other";
  }
}

/**
 * Google Play Integrity implementation of IPPAuthProvider.
 */
class IPPGpiAuthProviderSingleton extends IPPAuthProvider {
  #isGpiReady = false;
  #listener = null;
  #renewTimer = null;
  #enrollPromise = null;

  get helpers() {
    return [this];
  }

  init() {
    this.#listener = {
      onEvent: event => {
        if (event === "GeckoView:IPProtection:GPI:WarmUpCompleted") {
          this._onGpiWarmUpCompleted();
        } else {
          this._onGpiWarmUpFailed();
        }
      },
    };
    this._registerGpiListener(this.#listener);
    this._dispatchGpiWarmUp();

    this.#scheduleRenewal();
  }

  initOnStartupCompleted() {}

  uninit() {
    if (this.#listener) {
      this._unregisterGpiListener(this.#listener);
      this.#listener = null;
    }
    this.#isGpiReady = false;
    this.#cancelRenewalTimer();
  }

  // The methods below wrap EventDispatcher calls so that xpcshell tests can
  // stub them without pulling in the Android-only Messaging.sys.mjs module.

  _onGpiWarmUpCompleted() {
    this.#isGpiReady = true;
    lazy.IPProtectionService.updateState();
  }

  _onGpiWarmUpFailed() {
    lazy.IPProtectionActivator.reinitWithFallback();
  }

  _registerGpiListener(listener) {
    lazy.EventDispatcher.instance.registerListener(listener, [
      "GeckoView:IPProtection:GPI:WarmUpCompleted",
      "GeckoView:IPProtection:GPI:WarmUpFailed",
    ]);
  }

  _unregisterGpiListener(listener) {
    lazy.EventDispatcher.instance.unregisterListener(listener, [
      "GeckoView:IPProtection:GPI:WarmUpCompleted",
      "GeckoView:IPProtection:GPI:WarmUpFailed",
    ]);
  }

  _dispatchGpiWarmUp() {
    lazy.EventDispatcher.instance.dispatch(
      "GeckoView:IPProtection:GPI:WarmUp",
      {}
    );
  }

  #scheduleRenewal() {
    const renewAfter = Services.prefs.getCharPref(
      AUTH_JWT_RENEW_AFTER_PREF,
      ""
    );
    if (!renewAfter) {
      return;
    }
    this.#cancelRenewalTimer();
    const delay = Math.max(0, Number(renewAfter) - Date.now());
    this.#renewTimer = lazy.setTimeout(() => this.#enroll(), delay);
  }

  #cancelRenewalTimer() {
    if (this.#renewTimer !== null) {
      lazy.clearTimeout(this.#renewTimer);
      this.#renewTimer = null;
    }
  }

  #clearAuthJwt() {
    Services.prefs.clearUserPref(AUTH_JWT_PREF);
    Services.prefs.clearUserPref(AUTH_JWT_EXPIRES_AT_PREF);
    Services.prefs.clearUserPref(AUTH_JWT_RENEW_AFTER_PREF);
  }

  get #isJwtExpired() {
    const expiresAt = Services.prefs.getCharPref(AUTH_JWT_EXPIRES_AT_PREF, "");
    return !!expiresAt && Date.now() >= Number(expiresAt);
  }

  get #shouldRenewJwt() {
    const renewAfter = Services.prefs.getCharPref(
      AUTH_JWT_RENEW_AFTER_PREF,
      ""
    );
    return !!renewAfter && Date.now() >= Number(renewAfter);
  }

  // TODO: implement checkForUpgrade for GPI once the subscription flow is defined.
  async checkForUpgrade() {}

  async enroll() {
    const jwt = await this.#enroll();
    return {
      isEnrolledAndEntitled: !!jwt,
      error: jwt ? null : AUTH_ERRORS.ENROLLMENT_FAILED,
    };
  }

  get isReady() {
    const jwt = Services.prefs.getCharPref(AUTH_JWT_PREF, "");
    if (jwt) {
      if (this.#isJwtExpired) {
        this.#clearAuthJwt();
      } else {
        return true;
      }
    }
    return this.#isGpiReady;
  }

  async aboutToStart() {
    const needsEnrollment =
      !Services.prefs.getCharPref(AUTH_JWT_PREF, "") || this.#shouldRenewJwt;
    if (needsEnrollment) {
      const { isEnrolledAndEntitled } = await this.enroll();
      if (!isEnrolledAndEntitled) {
        return { error: AUTH_ERRORS.ENROLLMENT_FAILED };
      }
    }
    return null;
  }

  /**
   * Fetches a fresh GPI token from Android and enrolls with Guardian to obtain
   * a new Auth JWT.
   *
   * @param {AbortSignal} [abortSignal]
   * @returns {Promise<string|null>} The new Auth JWT, or null on failure.
   */
  async #enroll(abortSignal = null) {
    if (this.#enrollPromise) {
      return this.#enrollPromise;
    }

    const { promise, resolve } = Promise.withResolvers();
    this.#enrollPromise = promise;

    const gpiToken = await this._fetchGpiToken(abortSignal);
    if (!gpiToken) {
      Glean.ipprotection.gpiEnrollment.record({
        reason: "no_gpi_token",
        httpStatus: 0,
        hadPreviousJwt: !!Services.prefs.getCharPref(AUTH_JWT_PREF, ""),
      });
      this.#clearAuthJwt();
      this.#enrollPromise = null;
      resolve(null);
      return null;
    }

    const data = await this.#fetchAuthJwt(gpiToken);
    if (!data) {
      this.#clearAuthJwt();
      this.#enrollPromise = null;
      resolve(null);
      return null;
    }

    const { jwt, expiresAt, renewAfter } = data;
    Services.prefs.setCharPref(AUTH_JWT_PREF, jwt);
    Services.prefs.setCharPref(
      AUTH_JWT_EXPIRES_AT_PREF,
      typeof expiresAt === "number" ? String(expiresAt) : ""
    );
    Services.prefs.setCharPref(
      AUTH_JWT_RENEW_AFTER_PREF,
      typeof renewAfter === "number" ? String(renewAfter) : ""
    );
    this.#scheduleRenewal();
    this.#enrollPromise = null;
    resolve(jwt);
    return jwt;
  }

  /**
   * Requests a fresh GPI token from the Android layer.
   * Exposed as a non-private method so xpcshell tests can stub it without
   * pulling in the Android-only Messaging.sys.mjs module.
   *
   * @param {AbortSignal} [abortSignal]
   * @returns {Promise<string|null>} The GPI token, or null on failure.
   */
  async _fetchGpiToken(abortSignal = null) {
    try {
      const tasks = [
        lazy.EventDispatcher.instance.sendRequestForResult(
          "GeckoView:IPProtection:GPI:RequestToken",
          {}
        ),
      ];
      if (abortSignal) {
        tasks.push(
          new Promise((_, reject) => {
            abortSignal.addEventListener(
              "abort",
              () => reject(abortSignal.reason),
              { once: true }
            );
          })
        );
      }
      const result = await Promise.race(tasks);
      return result?.token ?? null;
    } catch {
      return null;
    }
  }

  get #guardianEndpoint() {
    return Services.prefs.getCharPref(
      GUARDIAN_ENDPOINT_PREF,
      GUARDIAN_ENDPOINT_DEFAULT
    );
  }

  /**
   * @param {string} gpiToken
   * @returns {Promise<{jwt: string, expiresAt: number|undefined, renewAfter: number|undefined}|null>}
   */
  async #fetchAuthJwt(gpiToken) {
    const url = new URL(this.#guardianEndpoint);
    url.pathname = "/api/v1/gpn/enrollment";

    const headers = { "Content-Type": "application/json" };
    const previousJwt = Services.prefs.getCharPref(AUTH_JWT_PREF, "");
    if (previousJwt) {
      headers.Authorization = `Bearer ${previousJwt}`;
    }

    const packageName = Services.env.get("MOZ_ANDROID_PACKAGE_NAME");

    let response;
    try {
      response = await fetch(url.href, {
        method: "POST",
        headers,
        body: JSON.stringify({ integrityToken: gpiToken, packageName }),
      });
    } catch {
      Glean.ipprotection.gpiEnrollment.record({
        reason: "network_error",
        httpStatus: 0,
        hadPreviousJwt: !!previousJwt,
      });
      return null;
    }

    if (!response.ok) {
      const text = await response.text().catch(() => "");
      Glean.ipprotection.gpiEnrollment.record({
        reason: categorizeEnrollmentReason(text),
        httpStatus: response.status,
        hadPreviousJwt: !!previousJwt,
      });
      return null;
    }

    try {
      const data = await response.json();
      const { deviceSessionJwt: jwt, expiresAt, renewAfter } = data ?? {};
      Glean.ipprotection.gpiEnrollment.record({
        reason: jwt ? "ok" : "bad_response",
        httpStatus: response.status,
        hadPreviousJwt: !!previousJwt,
      });
      return jwt ? { jwt, expiresAt, renewAfter } : null;
    } catch {
      Glean.ipprotection.gpiEnrollment.record({
        reason: "bad_response",
        httpStatus: response.status,
        hadPreviousJwt: !!previousJwt,
      });
      return null;
    }
  }

  /**
   * @param {AbortSignal} [abortSignal]
   * @returns {Promise<{error?: string, status?: number, pass?: ProxyPass, usage?: ProxyUsage|null, retryAfter?: string|null}>}
   */
  async fetchProxyPass(abortSignal = null) {
    return this.#fetchProxyPass(abortSignal, true);
  }

  async #fetchProxyPass(abortSignal, allowReenroll) {
    if (this.#enrollPromise) {
      await this.#enrollPromise;
    }

    const authJwt = Services.prefs.getCharPref(AUTH_JWT_PREF, "");
    if (!authJwt) {
      return { error: AUTH_ERRORS.LOGIN_NEEDED, usage: null };
    }

    const url = new URL(this.#guardianEndpoint);
    url.pathname = "/api/v1/gpn/token";

    let response;
    try {
      response = await fetch(url.href, {
        method: "GET",
        headers: { Authorization: `Bearer ${authJwt}` },
        signal: abortSignal,
      });
    } catch {
      return { error: AUTH_ERRORS.LOGIN_NEEDED, usage: null };
    }

    if (response.status === 401) {
      if (!allowReenroll) {
        return { status: 401, error: AUTH_ERRORS.UNAUTHORIZED, usage: null };
      }
      const newJwt = await this.#enroll(abortSignal);
      if (!newJwt) {
        return { status: 401, error: AUTH_ERRORS.UNAUTHORIZED, usage: null };
      }
      return this.#fetchProxyPass(abortSignal, false);
    }

    const status = response.status;

    let usage = null;
    try {
      usage = ProxyUsage.fromResponse(response);
    } catch {}

    const statusError = lazy.GuardianClient.toError(status);
    if (statusError) {
      const retryAfter =
        status === 429 ? response.headers.get("Retry-After") : null;
      return { status, error: statusError, usage, retryAfter };
    }

    try {
      const pass = await ProxyPass.fromResponse(response);
      if (!pass) {
        return { status, error: AUTH_ERRORS.INVALID_RESPONSE, usage };
      }
      return { pass, status, usage };
    } catch {
      return { status, error: AUTH_ERRORS.PARSE_ERROR, usage };
    }
  }

  /**
   * @param {AbortSignal} [abortSignal]
   * @returns {Promise<ProxyUsage|null>}
   */
  async fetchProxyUsage(abortSignal = null) {
    return this.#fetchProxyUsage(abortSignal, true);
  }

  async #fetchProxyUsage(abortSignal, allowReenroll) {
    if (this.#enrollPromise) {
      await this.#enrollPromise;
    }

    const authJwt = Services.prefs.getCharPref(AUTH_JWT_PREF, "");
    if (!authJwt) {
      return null;
    }

    const url = new URL(this.#guardianEndpoint);
    url.pathname = "/api/v1/gpn/token";

    let response;
    try {
      response = await fetch(url.href, {
        method: "HEAD",
        headers: { Authorization: `Bearer ${authJwt}` },
        signal: abortSignal,
      });
    } catch {
      return null;
    }

    if (response.status === 401) {
      if (!allowReenroll) {
        return null;
      }
      const newJwt = await this.#enroll(abortSignal);
      if (!newJwt) {
        return null;
      }
      return this.#fetchProxyUsage(abortSignal, false);
    }

    if (!response.ok) {
      return null;
    }

    try {
      return ProxyUsage.fromResponse(response);
    } catch {
      return null;
    }
  }
}

const IPPGpiAuthProvider = new IPPGpiAuthProviderSingleton();

export { IPPGpiAuthProvider, IPPGpiAuthProviderSingleton };
