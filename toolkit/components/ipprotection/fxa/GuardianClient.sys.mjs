/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import {
  Entitlement,
  ProxyPass,
  ProxyUsage,
} from "moz-src:///toolkit/components/ipprotection/GuardianTypes.sys.mjs";
import { AUTH_ERRORS } from "moz-src:///toolkit/components/ipprotection/IPPAuthProvider.sys.mjs";

/**
 * The subset of AuthError that a Guardian HTTP status maps onto.
 *
 * @typedef {"unauthorized" | "not_entitled" | "quota_exceeded" | "region_unavailable" | "server_error" | "unexpected_status"} GuardianStatusError
 *
 * The subset of AuthError that the Guardian client reports, i.e. the status
 * errors plus the ones raised without a usable response.
 *
 * @typedef {GuardianStatusError | "login_needed" | "invalid_response" | "parse_error"} GuardianError
 */

const lazy = {};

ChromeUtils.defineLazyGetter(
  lazy,
  "hiddenBrowserManager",
  () =>
    ChromeUtils.importESModule("resource://gre/modules/HiddenFrame.sys.mjs")
      .HiddenBrowserManager
);
ChromeUtils.defineLazyGetter(lazy, "logConsole", () =>
  console.createInstance({
    prefix: "GuardianClient",
    maxLogLevel: Services.prefs.getBoolPref("browser.ipProtection.log", false)
      ? "Debug"
      : "Warn",
  })
);

if (Services.appinfo.processType !== Services.appinfo.PROCESS_TYPE_DEFAULT) {
  throw new Error("Guardian.sys.mjs should only run in the parent process");
}

export const GUARDIAN_EXPERIMENT_TYPE = "alpha";

/**
 * An HTTP Client to talk to the Guardian service.
 * Allows to enroll users to the proxy service,
 * fetch a proxy pass and check if the user is a proxy user.
 *
 */
export class GuardianClient {
  constructor() {
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "guardianEndpoint",
      "browser.ipProtection.guardian.endpoint",
      "https://vpn.mozilla.com"
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "fxaOrigin",
      "identity.fxaccounts.remote.root"
    );
  }
  /**
   * Tries to enroll the user to the proxy service via a hidden browser sign-in flow.
   * The FxA OAuth flow is completed silently using the existing FxA session cookies.
   * If the user already has a proxy entitlement, the experiment type will update.
   *
   * @param { "alpha" | "beta" | "delta" | "gamma" } aExperimentType - The experiment type to enroll the user into.
   * The experiment type controls which feature set the user will get in Firefox.
   *
   * @param { AbortSignal | null } aAbortSignal - An AbortSignal to cancel the operation.
   * @returns {Promise<{error?: string, ok?: boolean}>}
   */
  async enrollWithFxa(
    aExperimentType = GUARDIAN_EXPERIMENT_TYPE,
    aAbortSignal = null
  ) {
    // We abort loading the page if the origin is not allowed.
    const allowedOrigins = [
      new URL(this.guardianEndpoint).origin,
      new URL(this.fxaOrigin).origin,
    ];
    const { loginURL, successURL, errorURL } =
      this.enrollmentURLs(aExperimentType);
    // If the browser is redirected to one of those urls
    // we know we're done with the browser.
    const finalizerURLs = [successURL, errorURL];
    return await lazy.hiddenBrowserManager.withHiddenBrowser(async browser => {
      const aborted = new Promise((_, reject) => {
        aAbortSignal?.addEventListener("abort", () => {
          browser.stop();
          browser.remove();
          reject(new Error("aborted"));
        });
      });
      const finalEndpoint = waitUntilURL(browser, url => {
        const urlObj = new URL(url);
        if (url === "about:blank") {
          return false;
        }
        if (!allowedOrigins.includes(urlObj.origin)) {
          browser.stop();
          browser.remove();
          throw new Error(
            `URL ${url} with origin ${urlObj.origin} is not allowed.`
          );
        }
        if (
          finalizerURLs.some(
            finalizer =>
              urlObj.pathname === finalizer.pathname &&
              urlObj.origin === finalizer.origin
          )
        ) {
          return true;
        }
        return false;
      });
      const loginURI = Services.io.newURI(loginURL.href);
      if (!allowedOrigins.includes(loginURL.origin)) {
        throw new Error(`Login URL origin ${loginURL.origin} is not allowed.`);
      }
      browser.loadURI(loginURI, {
        triggeringPrincipal:
          Services.scriptSecurityManager.createContentPrincipal(loginURI, {}),
      });

      const result = await Promise.race([finalEndpoint, aborted]);
      return GuardianClient._parseGuardianSuccessURL(result);
    });
  }

  static _parseGuardianSuccessURL(aUrl) {
    if (!aUrl) {
      return { error: "timeout", ok: false };
    }
    const url = new URL(aUrl);
    const params = new URLSearchParams(url.search);
    const error = params.get("error");
    if (error) {
      return { error, ok: false };
    }
    // Otherwise we should have:
    // - a code in the URL query
    if (!params.has("code")) {
      return { error: "missing_code", ok: false };
    }
    return { ok: true };
  }

  /**
   * Maps a Guardian HTTP status onto the meaning Guardian assigns to it.
   *
   * @param {number} status
   * @returns {GuardianStatusError | null} Null when the status carries no error.
   */
  static toError(status) {
    switch (status) {
      case 200:
        // A pass was issued.
        return null;
      case 401:
        // The auth token was rejected.
        return AUTH_ERRORS.UNAUTHORIZED;
      case 403:
        // The auth token was valid, but the user has no entitlement assigned.
        return AUTH_ERRORS.NOT_ENTITLED;
      case 429:
        // The usage quota for this period is spent.
        return AUTH_ERRORS.QUOTA_EXCEEDED;
      case 451:
        // Guardian does not serve this region.
        return AUTH_ERRORS.REGION_UNAVAILABLE;
      default:
        if (status >= 500 && status <= 599) {
          // Guardian failed to answer.
          return AUTH_ERRORS.SERVER_ERROR;
        }
        // Guardian assigns no meaning to this status.
        return AUTH_ERRORS.UNEXPECTED_STATUS;
    }
  }

  /**
   * Fetches a proxy pass from the Guardian service.
   *
   * A status other than 200 always resolves with an error, see
   * {@link GuardianClient.toError}.
   *
   * @param {{token: string}} tokenHandle - short-lived OAuth token obtained from fxAccounts
   * @param {AbortSignal} [abortSignal=null] - a signal to indicate the fetch should be aborted
   * @returns {Promise<{error?: GuardianError, status?:number, pass?: ProxyPass, usage?: ProxyUsage|null, retryAfter?: string|null}>} Resolves with an object containing either an error or the proxy pass data and a status code.
   */
  async fetchProxyPass(tokenHandle, abortSignal = null) {
    const response = await fetch(this.#tokenURL, {
      method: "GET",
      cache: "no-cache",
      headers: {
        Authorization: `Bearer ${tokenHandle.token}`,
        "Content-Type": "application/json",
      },
      signal: abortSignal,
    });
    if (!response) {
      return { error: AUTH_ERRORS.LOGIN_NEEDED, usage: null };
    }
    const status = response.status;

    let usage = null;
    try {
      usage = ProxyUsage.fromResponse(response);
    } catch (error) {
      lazy.logConsole.warn(
        "Usage headers missing or invalid, continuing without usage:",
        error
      );
    }

    const statusError = GuardianClient.toError(status);
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
    } catch (error) {
      lazy.logConsole.error("Error parsing pass:", error);
      return { status, error: AUTH_ERRORS.PARSE_ERROR, usage };
    }
  }
  /**
   * Fetches the user's entitlement information.
   *
   * @param {{token: string}} tokenHandle - short-lived OAuth token obtained from fxAccounts
   * @param {AbortSignal} [abortSignal=null] - a signal to indicate the fetch should be aborted
   * @returns {Promise<{status?: number, entitlement?: Entitlement|null, error?:string}>} A promise that resolves to an object containing the HTTP status code and the user's entitlement information.
   *
   * Status codes to watch for:
   * - 200: User is a proxy user and the entitlement information is available.
   * - 404: User is not a proxy user, no entitlement information available.
   * - 401: The auth token was rejected, probably a guardian/auth provider environment mismatch.
   */
  async fetchUserInfo(tokenHandle, abortSignal = null) {
    const response = await fetch(this.#statusURL, {
      method: "GET",
      headers: {
        Authorization: `Bearer ${tokenHandle.token}`,
        "Content-Type": "application/json",
      },
      cache: "no-cache",
      signal: abortSignal,
    });
    if (!response) {
      return { error: AUTH_ERRORS.LOGIN_NEEDED };
    }
    const status = response.status;
    try {
      const entitlement = await Entitlement.fromResponse(response);
      if (!entitlement) {
        return { status, error: AUTH_ERRORS.PARSE_ERROR };
      }
      return {
        status,
        entitlement,
      };
    } catch (error) {
      return { status, error: AUTH_ERRORS.PARSE_ERROR };
    }
  }

  /**
   * Returns the user's proxy usage information, without fetching a new proxy pass.
   *
   * @param {{token: string}} tokenHandle - short-lived OAuth token obtained from fxAccounts
   * @param {AbortSignal} abortSignal - Signal for when this function should be aborted
   * @returns {ProxyUsage | null}
   */
  async fetchProxyUsage(tokenHandle, abortSignal) {
    const response = await fetch(this.#tokenURL, {
      method: "HEAD",
      cache: "no-cache",
      signal: abortSignal,
      headers: {
        Authorization: `Bearer ${tokenHandle.token}`,
        "Content-Type": "application/json",
      },
    });
    if (!response) {
      return null;
    }
    try {
      return ProxyUsage.fromResponse(response);
    } catch (error) {
      lazy.logConsole.warn(
        "Usage headers missing or invalid, continuing without usage:",
        error
      );
    }
    return null;
  }

  /**
   * Activates the current FxA account with Guardian by presenting the FxA
   * Bearer token directly.
   *
   * @param {{token: string}} tokenHandle - short-lived OAuth token obtained from fxAccounts
   * @param {AbortSignal} [abortSignal=null]
   * @returns {Promise<{ok: boolean, entitlement?: object, error?: string}>}
   */
  async activate(tokenHandle, abortSignal = null) {
    if (!tokenHandle) {
      return { ok: false, error: AUTH_ERRORS.LOGIN_NEEDED };
    }
    const response = await fetch(this.#activateURL, {
      method: "POST",
      cache: "no-cache",
      headers: {
        Authorization: `Bearer ${tokenHandle.token}`,
        "Content-Type": "application/json",
      },
      signal: abortSignal,
    });
    if (!response.ok) {
      return { ok: false, error: `status_${response.status}` };
    }
    try {
      const entitlement = await Entitlement.fromResponse(response);
      return { ok: true, entitlement };
    } catch (error) {
      return { ok: false, error: AUTH_ERRORS.PARSE_ERROR };
    }
  }

  get #activateURL() {
    const url = new URL(this.guardianEndpoint);
    url.pathname = "/api/v1/fpn/activate";
    return url;
  }

  /** This is the URL that will be used to fetch the proxy pass. */
  get #tokenURL() {
    const url = new URL(this.guardianEndpoint);
    url.pathname = "/api/v1/fpn/token";
    return url;
  }
  /**
   * Returns the URLs needed to perform FxA enrollment with Guardian.
   *
   * @param {"alpha"|"beta"|"delta"|"gamma"} experimentType
   * @returns {{ loginURL: URL, successURL: URL, errorURL: URL }}
   */
  enrollmentURLs(experimentType = GUARDIAN_EXPERIMENT_TYPE) {
    const loginURL = new URL(this.guardianEndpoint);
    loginURL.pathname = "/api/v1/fpn/auth";
    loginURL.searchParams.set("experiment", experimentType);

    const successURL = new URL(this.guardianEndpoint);
    successURL.pathname = "/oauth/success";

    const errorURL = new URL(this.guardianEndpoint);
    errorURL.pathname = "/api/v1/fpn/error";

    return { loginURL, successURL, errorURL };
  }
  /** This is the URL that will be used to check the user's proxy status. */
  get #statusURL() {
    const url = new URL(this.guardianEndpoint);
    url.pathname = "/api/v1/fpn/status";
    return url;
  }
  guardianEndpoint = "";
}

/**
 * Adds a strong reference to keep listeners alive until
 * we're done with it.
 * (From kungFuDeathGrip in XPCShellContentUtils.sys.mjs)
 */
const listeners = new Set();

/**
 * Waits for a specific URL to be loaded in the browser.
 *
 * @param {*} browser - The browser instance to listen for URL changes.
 * @param {(location: string) => boolean} predicate - A function that returns true if the location matches the desired URL.
 * @returns {Promise<string>} A promise that resolves to the matching URL.
 */
async function waitUntilURL(browser, predicate) {
  const prom = Promise.withResolvers();
  let done = false;
  const check = arg => {
    if (done) {
      return;
    }
    if (predicate(arg)) {
      done = true;
      listeners.delete(listener);
      browser.removeProgressListener(listener);
      prom.resolve(arg);
    }
  };
  const listener = {
    QueryInterface: ChromeUtils.generateQI([
      "nsIWebProgressListener",
      "nsISupportsWeakReference",
    ]),

    // Runs the check after the document has stopped loading.
    onStateChange(webProgress, request, stateFlags, status) {
      request.QueryInterface(Ci.nsIChannel);

      if (
        webProgress.isTopLevel &&
        stateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
        status !== Cr.NS_BINDING_ABORTED
      ) {
        check(request.URI?.spec);
      }
    },

    // Unused callbacks we still need to implement:
    onLocationChange() {},
    onProgressChange() {},
    onStatusChange(_, request, status) {
      if (Components.isSuccessCode(status)) {
        return;
      }
      try {
        const url = request.QueryInterface(Ci.nsIChannel).URI.spec;
        check(url);
      } catch (ex) {}
    },
    onSecurityChange() {},
    onContentBlockingEvent() {},
  };
  listeners.add(listener);
  browser.addProgressListener(listener, Ci.nsIWebProgress.NOTIFY_STATE_WINDOW);
  const url = await prom.promise;
  return url;
}
