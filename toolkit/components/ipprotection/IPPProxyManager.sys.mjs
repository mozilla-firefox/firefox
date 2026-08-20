/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AUTH_ERRORS } from "moz-src:///toolkit/components/ipprotection/IPPAuthProvider.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  IPPChannelFilter:
    "moz-src:///toolkit/components/ipprotection/IPPChannelFilter.sys.mjs",
  IPPLifecycleHelper:
    "moz-src:///toolkit/components/ipprotection/IPPLifecycleHelper.sys.mjs",
  IPPNetworkUtils:
    "moz-src:///toolkit/components/ipprotection/IPPNetworkUtils.sys.mjs",
  IPPNetworkErrorObserver:
    "moz-src:///toolkit/components/ipprotection/IPPNetworkErrorObserver.sys.mjs",
  IPProtectionServerlist:
    "moz-src:///toolkit/components/ipprotection/IPProtectionServerlist.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  IPProtectionStates:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  IPPStartupCache:
    "moz-src:///toolkit/components/ipprotection/IPPStartupCache.sys.mjs",
});

ChromeUtils.defineLazyGetter(
  lazy,
  "setTimeout",
  () =>
    ChromeUtils.importESModule("resource://gre/modules/Timer.sys.mjs")
      .setTimeout
);
ChromeUtils.defineLazyGetter(
  lazy,
  "clearTimeout",
  () =>
    ChromeUtils.importESModule("resource://gre/modules/Timer.sys.mjs")
      .clearTimeout
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "timeout",
  "browser.ipProtection.guardian.timeout",
  Temporal.Duration.from({ seconds: 30 }).total("milliseconds")
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "retryAfter",
  "browser.ipProtection.guardian.retryAfter",
  500
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "attemptTimeout",
  "browser.ipProtection.guardian.attemptTimeout",
  Temporal.Duration.from({ seconds: 10 }).total("milliseconds")
);

// Floor for scheduled rotations. A freshly fetched pass that is already due for
// rotation would otherwise schedule with a zero delay, and since a successful
// rotation reschedules the next one, that spins into a tight rotation loop.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "minRotationInterval",
  "browser.ipProtection.guardian.minRotationInterval",
  Temporal.Duration.from({ seconds: 1 }).total("milliseconds")
);

export const ERRORS = Object.freeze({
  GENERIC: "generic-error",
  NETWORK: "network-error",
  CATASTROPHIC: "catastrophic-error",
  TIMEOUT: "timeout-error", // Activation took too long and was aborted
  MISSING_PROMISE: "missing-activation-promise", // Expected promise was not returned
  MISSING_ABORT: "missing-abort-controller", // Expected abort controller was not returned
  MISSING_PASS: "missing-pass", // A rotation completed without a pass or an error
  PASS_UNAVAILABLE: "pass-unavailable", // No pass was returned from the server
  SERVER_NOT_FOUND: "server-not-found", // No server was found for the location
  SERVERLIST_UNAVAILABLE: "serverlist-unavailable", // The server list could not be fetched
  CANCELED: "activation-canceled", // Activation was canceled
  VPN_UNAVAILABLE: "vpn-unavailable", // VPN unavailable in local region
  NOT_READY: "not-ready-error", // Activation was requested outside of the ready state
  QUOTA_EXHAUSTED: "quota-exhausted", // The bandwidth limit has been reached
  CONNECTION_FAILED: "connection-failed", // The proxy connection never came up

  /**
   * Maps an auth provider error onto this vocabulary. Causes without a name of
   * our own pass through unchanged.
   *
   * @param {import("./IPPAuthProvider.sys.mjs").AuthError} error
   * @returns {string}
   */
  from(error) {
    switch (error) {
      case AUTH_ERRORS.SERVER_ERROR:
        return ERRORS.CATASTROPHIC;
      case AUTH_ERRORS.REGION_UNAVAILABLE:
        return ERRORS.VPN_UNAVAILABLE;
      case AUTH_ERRORS.QUOTA_EXCEEDED:
        return ERRORS.QUOTA_EXHAUSTED;
      default:
        return error;
    }
  },
});

const LOG_PREF = "browser.ipProtection.log";

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    prefix: "IPPProxyManager",
    maxLogLevel: Services.prefs.getBoolPref(LOG_PREF, false) ? "Debug" : "Warn",
  });
});

/**
 * A Type containing the states of the IPPProxyManager.
 *
 * @typedef {"not-ready" | "ready" | "activating" | "active" | "error" | "paused"} IPPProxyState
 * An Object containing instances of the IPPProxyState
 * @typedef {object} IPPProxyStates
 *
 * @property {string} NOT_READY
 *  The proxy is not ready because the main state machine is not in the READY state.
 * @property {string} READY
 *  The proxy is ready to be activated.
 * @property {string} ACTIVE
 *  The proxy is active.
 * @property {string} ERROR
 *  An error occurred while the proxy was active.
 * @property {string} PAUSED
 *  The VPN is paused i.e when the bandwidth limit is reached.
 *
 * Note: If you update this list of states, make sure to update the
 * corresponding documentation in the `docs` folder as well.
 */
export const IPPProxyStates = Object.freeze({
  NOT_READY: "not-ready",
  READY: "ready",
  ACTIVATING: "activating",
  ACTIVE: "active",
  ERROR: "error",
  PAUSED: "paused",
});

/**
 * Schedules a callback to be triggered at a specific timepoint.
 *
 * Allows to schedule callbacks further in the future than setTimeout.
 *
 * @param {Function} callback  - the callback to trigger when the timepoint is reached
 * @param {Temporal.Instant} timepoint - the time to trigger the callback
 * @param {AbortSignal} abortSignal - signal to cancel the scheduled callback
 * @param {object} imports - dependencies for this function, mainly for testing
 */
export async function scheduleCallback(
  callback,
  timepoint,
  abortSignal,
  imports = lazy
) {
  const getNow = imports.getNow || (() => Temporal.Now.instant());
  while (getNow().until(timepoint).total("milliseconds") > 0) {
    if (abortSignal.aborted) {
      return;
    }
    const msUntilTrigger = getNow().until(timepoint).total("milliseconds");
    // clamp the timeout to the max allowed by setTimeout
    const clampedMs = Math.min(msUntilTrigger, 2147483647);
    await new Promise(resolve => {
      const timeoutId = imports.setTimeout(resolve, clampedMs);
      abortSignal.addEventListener(
        "abort",
        () => {
          imports.clearTimeout(timeoutId);
          resolve();
        },
        { once: true }
      );
    });
  }
  if (abortSignal.aborted) {
    return;
  }
  callback();
}

/**
 * Manages the proxy connection for the IPProtectionService.
 */
class IPPProxyManagerSingleton extends EventTarget {
  /** @type {IPPProxyState}  */
  #state = IPPProxyStates.NOT_READY;

  #activatingPromise = null;
  #activationAbortController = null;

  #pass = null;
  /**@type {import("./GuardianTypes.sys.mjs").ProxyUsage | null} */
  #usage = null;
  /**@type {import("./IPPChannelFilter.sys.mjs").IPPChannelFilter | null} */
  #connection = null;
  #networkErrorObserver = null;
  // If this is set, we're awaiting a proxy pass rotation.
  /** @type {{ promise: Promise<void>, controller: AbortController } | null} */
  #rotation = null;
  #activatedAt = 0;

  #rotationTimer = 0;
  #usageRefreshAbortController = null;
  /** @type {string | null} */
  #errorType = null;
  #refreshUsageAbortController = null;

  constructor() {
    super();

    this.setErrorState = this.#setErrorState.bind(this);
    this.handleProxyErrorEvent = this.#handleProxyErrorEvent.bind(this);
    this.handleEvent = this.#handleEvent.bind(this);
  }

  init() {
    lazy.IPProtectionService.addEventListener(
      "IPProtectionService:StateChanged",
      this.handleEvent
    );

    if (!this.#usage) {
      this.#usage = lazy.IPPStartupCache.usageInfo;
    }

    // If we loaded a cached usage metric that was reset in the past
    // (e.g. Firefox launched on/after the quota's monthly rollover),
    // try to refresh immediately so we don't display last month's value.
    if (this.#usage && !this.#usage.unlimited) {
      this.#scheduleUsageCheck(this.#usage);
    }
  }

  initOnStartupCompleted() {}

  uninit() {
    lazy.IPProtectionService.removeEventListener(
      "IPProtectionService:StateChanged",
      this.handleEvent
    );

    if (
      this.#state === IPPProxyStates.ACTIVE ||
      this.#state === IPPProxyStates.ACTIVATING
    ) {
      this.stop(false);
    }
    if (this.#usageRefreshAbortController) {
      this.#usageRefreshAbortController.abort();
      this.#usageRefreshAbortController = null;
    }

    this.#pass = null;
    this.#usage = null;
    this.#connection = null;
  }

  /**
   * Checks if the proxy is active and was activated.
   *
   * @returns {Date}
   */
  get activatedAt() {
    return this.#state === IPPProxyStates.ACTIVE && this.#activatedAt;
  }

  get networkErrorObserver() {
    if (!this.#networkErrorObserver) {
      this.#networkErrorObserver = new lazy.IPPNetworkErrorObserver();
      this.#networkErrorObserver.addEventListener(
        "proxy-http-error",
        this.handleProxyErrorEvent
      );
    }
    return this.#networkErrorObserver;
  }

  get active() {
    return this.#connection?.active;
  }

  get isolationKey() {
    return this.#connection?.isolationKey;
  }

  get hasValidProxyPass() {
    return !!this.#pass?.isValid();
  }

  /**
   * Gets the current usage info.
   * This will be updated on every new ProxyPass fetch,
   * changes to the usage will be notified via the "IPPProxyManager:UsageChanged" event.
   *
   * @returns {import("./GuardianTypes.sys.mjs").ProxyUsage | null}
   */
  get usageInfo() {
    return this.#usage;
  }

  channelFilter() {
    return this.#connection;
  }

  createChannelFilter() {
    if (!this.#connection) {
      this.#connection = lazy.IPPChannelFilter.create();
      this.#connection.start();
    }
  }

  cancelChannelFilter() {
    if (this.#connection) {
      this.#connection.stop();
      this.#connection = null;
    }
  }

  get state() {
    return this.#state;
  }

  get errorType() {
    return this.#errorType;
  }

  /**
   * Start the proxy if the user is eligible.
   *
   * @param {boolean} userAction
   * True if started by user action, false if system action
   * @param {boolean} inPrivateBrowsing
   * True if started from a private browsing window
   * @param {string} [country]
   * Optional ISO 3166-1 alpha-2 country code to route through. When
   * omitted, the recommended (anycast) location is used.
   * @returns {Promise<{started: boolean, error?: string}>}
   * Started is true if successfully connected, error contains the error message if it fails.
   */
  async start(userAction = true, inPrivateBrowsing = false, country) {
    if (this.#state === IPPProxyStates.ACTIVATING) {
      if (!this.#activatingPromise) {
        throw new Error(ERRORS.MISSING_PROMISE);
      }

      return this.#activatingPromise;
    }

    if (this.#state === IPPProxyStates.NOT_READY) {
      return { started: false, error: ERRORS.NOT_READY };
    }

    if (this.#state === IPPProxyStates.PAUSED) {
      return { started: false, error: ERRORS.QUOTA_EXHAUSTED };
    }

    if (this.#state === IPPProxyStates.ERROR) {
      return { started: false, error: this.#errorType ?? ERRORS.GENERIC };
    }

    this.#activationAbortController = new AbortController();
    const abortSignal = this.#activationAbortController.signal;

    // Abort the activation if it takes more than 30 seconds, or if the user cancels it.
    lazy.setTimeout(
      () => {
        this.#activationAbortController?.abort(ERRORS.TIMEOUT);
      },
      Temporal.Duration.from({ seconds: 30 }).total("milliseconds")
    );

    this.#setState(IPPProxyStates.ACTIVATING);

    const { promise: abortPromise, reject } = Promise.withResolvers();
    abortSignal.addEventListener(
      "abort",
      () => {
        reject(abortSignal.reason);
      },
      { once: true }
    );

    this.#activatingPromise = Promise.race([
      this.#startInternal(abortSignal, country),
      abortPromise,
    ])
      .then(
        started => {
          if (this.#state === IPPProxyStates.PAUSED) {
            return { started: false, error: ERRORS.QUOTA_EXHAUSTED };
          }
          if (this.#state === IPPProxyStates.ERROR) {
            return { started: false, error: this.#errorType ?? ERRORS.GENERIC };
          }
          // Proxy failed to start but no error was given.
          if (!started) {
            this.cancelChannelFilter();
            this.updateState();
            return { started: false, error: ERRORS.GENERIC };
          }
          this.#setState(IPPProxyStates.ACTIVE);
          Glean.ipprotection.started.record({
            userAction,
            inPrivateBrowsing,
          });
          return { started: true };
        },
        error => {
          this.#activationAbortController = null;
          this.cancelChannelFilter();
          this.#setErrorState(error);
          return { started: false, error };
        }
      )
      .finally(() => {
        this.#activatingPromise = null;
        this.#activationAbortController = null;
      });
    return this.#activatingPromise;
  }

  async #startInternal(abortSignal, country) {
    // Check network status before attempting connection
    if (lazy.IPPNetworkUtils.isOffline) {
      throw ERRORS.NETWORK;
    }

    try {
      await lazy.IPProtectionServerlist.maybeFetchList();
    } catch (e) {
      lazy.logConsole.error("Serverlist fetch failed:", e);
      throw ERRORS.SERVERLIST_UNAVAILABLE;
    }

    const notReady = await lazy.IPProtectionService.authProvider.aboutToStart();
    if (notReady) {
      throw ERRORS.from(notReady.error) || ERRORS.GENERIC;
    }

    // Check if we aborted before starting the channel filter.
    if (abortSignal?.aborted) {
      throw abortSignal.reason ?? ERRORS.CANCELED;
    }

    this.createChannelFilter();

    // If the current proxy pass is valid, no need to re-authenticate.
    // Throws an error if the proxy pass is not available.
    if (this.#pass == null || this.#pass.shouldRotate()) {
      const { pass, usage, error } = await this.#getPassAndUsage(abortSignal);
      if (usage) {
        this.#setUsage(usage);
        if (usage.quotaExhausted) {
          this.#setPausedState();
          return false;
        }
      }

      if (error || !pass) {
        throw typeof error === "string"
          ? ERRORS.from(error)
          : ERRORS.PASS_UNAVAILABLE;
      }
      this.#pass = pass;
    }
    this.#schedulePassRotation(this.#pass);

    const location = country
      ? lazy.IPProtectionServerlist.getLocation(country)
      : lazy.IPProtectionServerlist.getRecommendedLocation();
    const server = lazy.IPProtectionServerlist.selectServer(location?.city);
    if (!server) {
      throw ERRORS.SERVER_NOT_FOUND;
    }

    lazy.logConsole.debug("Server:", server?.hostname);

    this.#connection.initialize(this.#pass, server);

    this.networkErrorObserver.start();
    this.networkErrorObserver.addIsolationKey(this.#connection.isolationKey);

    lazy.logConsole.info("Started");

    if (!!this.#connection?.active && !!this.#connection?.proxyInfo) {
      this.#activatedAt = ChromeUtils.now();
      return true;
    }

    throw ERRORS.CONNECTION_FAILED;
  }

  /**
   * Stops the proxy.
   *
   * @param {boolean} userAction
   * True if started by user action, false if system action
   */
  async stop(userAction = true) {
    if (this.#state === IPPProxyStates.ACTIVATING) {
      if (!this.#activatingPromise) {
        throw new Error(ERRORS.MISSING_PROMISE);
      }
      if (!this.#activationAbortController) {
        throw new Error(ERRORS.MISSING_ABORT);
      }
      this.#activationAbortController?.abort(ERRORS.CANCELED);
      await this.#activatingPromise.then(() => this.stop(userAction));
      return;
    }

    if (
      this.#state !== IPPProxyStates.PAUSED &&
      this.#state !== IPPProxyStates.ACTIVE &&
      this.#state !== IPPProxyStates.ERROR
    ) {
      return;
    }

    if (this.#connection) {
      this.cancelChannelFilter();

      lazy.clearTimeout(this.#rotationTimer);
      this.#rotationTimer = 0;

      this.#rotation?.controller.abort();

      this.networkErrorObserver.stop();
    }

    lazy.logConsole.info("Stopped");

    const sessionLength = this.#activatedAt
      ? ChromeUtils.now() - this.#activatedAt
      : 0;

    Glean.ipprotection.stopped.record({
      userAction,
      duration: String(sessionLength),
    });
    this.updateState();
    if (
      userAction &&
      this.#state !== IPPProxyStates.PAUSED &&
      !this.#usage?.unlimited
    ) {
      this.refreshUsage();
    }
  }

  /**
   * Switch the active proxy connection to a server in a different country.
   *
   * @param {string} country - country code
   * @returns {{switched: boolean, error?: string}}
   */
  switch(country) {
    if (this.#state !== IPPProxyStates.ACTIVE) {
      return { switched: false, error: ERRORS.NOT_READY };
    }

    const location = country
      ? lazy.IPProtectionServerlist.getLocation(country)
      : lazy.IPProtectionServerlist.getRecommendedLocation();
    const server = lazy.IPProtectionServerlist.selectServer(location?.city);

    if (!server) {
      this.#setErrorState(ERRORS.SERVER_NOT_FOUND);
      return { switched: false, error: ERRORS.SERVER_NOT_FOUND };
    }

    lazy.logConsole.debug("Switching to server:", server?.hostname);

    this.#connection.suspend();
    this.#connection.initialize(this.#pass, server);

    this.networkErrorObserver.addIsolationKey(this.#connection.isolationKey);

    return { switched: true };
  }

  /**
   * Stop any connections and reset the pass and usage if the user has changed.
   */
  async reset() {
    this.#refreshUsageAbortController?.abort();
    this.#rotation?.controller.abort();

    this.#pass = null;
    this.#usage = null;
    if (this.#usageRefreshAbortController) {
      this.#usageRefreshAbortController.abort();
      this.#usageRefreshAbortController = null;
    }
    lazy.IPPStartupCache.storeUsageInfo(null);
    if (
      this.#state === IPPProxyStates.ACTIVE ||
      this.#state === IPPProxyStates.ACTIVATING ||
      this.#state === IPPProxyStates.PAUSED ||
      this.#state === IPPProxyStates.ERROR
    ) {
      await this.stop();
    }
  }

  /**
   * Move to the PAUSED state and close the connection,
   * but leave the channel filter in place if state was ACTIVE.
   *
   * Usage refresh will still be attempted at the reset time.
   */
  #setPausedState() {
    this.#rotation?.controller.abort();

    const wasActive = this.#state === IPPProxyStates.ACTIVE;
    this.#pass = null;
    lazy.clearTimeout(this.#rotationTimer);
    this.#rotationTimer = 0;

    if (wasActive) {
      this.#connection?.suspend();
    } else {
      this.cancelChannelFilter();
    }

    Glean.ipprotection.paused.record({
      wasActive,
    });

    this.#setState(IPPProxyStates.PAUSED);
  }

  async #handleEvent(event) {
    const { state, prevState } = event.detail;
    if (state !== lazy.IPProtectionStates.READY) {
      await this.reset();
    } else if (prevState !== lazy.IPProtectionStates.READY) {
      this.refreshUsage();
    }
    this.updateState();
  }

  /**
   * Fetches a new ProxyPass.
   * Throws an error on failures.
   *
   * @param {AbortSignal} [abortSignal=null] - a signal to indicate the fetch should be aborted, will then throw an AbortError
   * @returns {Promise<{pass?: ProxyPass | null, usage?: ProxyUsage | null, error?: import("./IPPAuthProvider.sys.mjs").AuthError, status?: number}>}
   */
  async #getPassAndUsage(abortSignal = null) {
    let { status, error, pass, usage } =
      await lazy.IPProtectionService.authProvider.fetchProxyPass(abortSignal);
    lazy.logConsole.debug("ProxyPass:", {
      status,
      valid: pass?.isValid(),
      error,
    });

    // Handle quota exceeded as a special case - return null pass with usage
    if (error === AUTH_ERRORS.QUOTA_EXCEEDED) {
      lazy.logConsole.info("Quota exceeded", {
        usage: usage ? `${usage.remaining} / ${usage.max}` : "unknown",
      });
      return { pass: null, usage, error, status };
    }

    if (error) {
      return { error, status };
    }

    return { pass, usage, status };
  }

  /**
   * Attempts getting a new ProxyPass and usage info, retrying on transient errors
   * until the abort signal is triggered.
   *
   * @param {AbortSignal} abortSignal
   * @returns {Promise<object|null>}
   */
  async #attemptPassRotation(abortSignal) {
    let delay = lazy.retryAfter;
    while (!abortSignal.aborted) {
      let result;
      const attemptController = new AbortController();
      const attemptTimer = lazy.setTimeout(
        () => attemptController.abort(ERRORS.TIMEOUT),
        lazy.attemptTimeout
      );
      const attemptSignal = AbortSignal.any([
        abortSignal,
        attemptController.signal,
      ]);
      try {
        result = await this.#getPassAndUsage(attemptSignal);
      } catch (e) {
        if (abortSignal.aborted) {
          return null;
        }
        // A per-attempt timeout (attemptSignal aborted) or an offline error is
        // transient: back off and retry within the overall budget instead of
        // failing the whole rotation.
        if (!attemptSignal.aborted && !lazy.IPPNetworkUtils.isOffline) {
          throw e;
        }
      } finally {
        lazy.clearTimeout(attemptTimer);
      }
      if (result && !(result.status >= 500 && result.status <= 599)) {
        return result;
      }
      await scheduleCallback(
        () => {
          delay *= 2;
        },
        Temporal.Now.instant().add({ milliseconds: delay }),
        abortSignal
      );
    }
    return null;
  }

  /**
   * Given a ProxyPass, sets a timer and triggers a rotation when it's about to expire.
   *
   * @param {*} pass
   */
  #schedulePassRotation(pass) {
    if (this.#rotationTimer) {
      lazy.clearTimeout(this.#rotationTimer);
      this.#rotationTimer = 0;
    }

    const now = Temporal.Now.instant();
    const rotationTimePoint = pass.rotationTimePoint;
    // Floor the delay: an already-due pass would otherwise schedule immediately
    // and, since a successful rotation reschedules, spin into a tight loop.
    let msUntilRotation = Math.max(
      now.until(rotationTimePoint).total("milliseconds"),
      lazy.minRotationInterval
    );

    lazy.logConsole.debug(
      `ProxyPass will rotate in ${now.until(rotationTimePoint).total("minutes")} minutes`
    );
    this.#rotationTimer = lazy.setTimeout(async () => {
      this.#rotationTimer = 0;
      if (!this.#connection?.active) {
        return;
      }
      if (lazy.IPPLifecycleHelper.isSuspended) {
        // Asleep/backgrounded: the wake handler will rotate once network is back.
        return;
      }
      lazy.logConsole.debug(`Starting scheduled ProxyPass rotation`);
      await this.rotateProxyPass();
    }, msUntilRotation);
  }

  /**
   * Starts a flow to get a new ProxyPass and replace the current one.
   *
   * @returns {Promise<void|ProxyPass>} - Returns a promise that resolves when the rotation is complete or failed.
   * When it's called again while a rotation is in progress, it will return the existing promise.
   */
  async rotateProxyPass() {
    if (this.#rotation) {
      return this.#rotation.promise;
    }
    const controller = new AbortController();
    const timeoutId = lazy.setTimeout(() => {
      controller.abort(ERRORS.TIMEOUT);
    }, lazy.timeout);
    let { promise, resolve } = Promise.withResolvers();
    this.#rotation = { promise, controller };
    let resumed = false;
    using scopeGuard = new DisposableStack();
    scopeGuard.defer(() => {
      lazy.clearTimeout(timeoutId);
      if (!resumed) {
        this.#connection?.abortPendingChannels();
      }
      resolve();
      this.#rotation = null;
    });

    if (this.#connection?.active) {
      this.#connection.suspend();
    }

    let result = await this.#attemptPassRotation(controller.signal);
    if (controller.signal.aborted || !result) {
      if (controller.signal.reason === ERRORS.TIMEOUT) {
        this.#setErrorState(ERRORS.TIMEOUT);
      }
      return null;
    }

    const { pass, usage, error } = result;
    if (usage) {
      this.#setUsage(usage);
      if (usage.quotaExhausted) {
        this.#setPausedState();
        return null;
      }
    }

    if (error) {
      this.#setErrorState(error);
      return null;
    }

    if (!pass) {
      lazy.logConsole.debug("Failed to rotate token!");
      this.#setErrorState(ERRORS.MISSING_PASS);
      return null;
    }
    // Inject the new token in the current connection
    if (this.#connection?.active) {
      this.#connection.replaceAuthTokenAndResume(pass);
      this.networkErrorObserver.addIsolationKey(this.#connection.isolationKey);
      resumed = true;
    }
    lazy.logConsole.debug("Successfully rotated token!");
    this.#pass = pass;
    // The freshly rotated token supersedes any pending scheduled rotation;
    // cancel it and schedule the next one for this pass.
    this.#schedulePassRotation(pass);
    resolve(pass);
    return promise;
  }

  /**
   * Refreshes the usage info from the server.
   * Can move the state:
   *  Active -> Paused if the usage is exhausted
   *  Not-Ready -> Ready if the service becomes ready and usage is available
   *
   * Will move the state to PAUSED if no usage is available.
   *
   * @return {Promise<void>}
   */
  async refreshUsage() {
    this.#refreshUsageAbortController?.abort();
    this.#refreshUsageAbortController = new AbortController();
    const { signal } = this.#refreshUsageAbortController;
    let newUsage;
    try {
      newUsage =
        await lazy.IPProtectionService.authProvider.fetchProxyUsage(signal);
    } catch (error) {
      lazy.logConsole.error("Error refreshing usage:", error);
    } finally {
      this.#refreshUsageAbortController = null;
    }
    if (!newUsage) {
      lazy.logConsole.debug("Failed to refresh usage info!");
      return;
    }
    this.#setUsage(newUsage);
    this.updateState();
  }

  #handleProxyErrorEvent(event) {
    if (!this.#connection?.active) {
      return null;
    }
    const { isolationKey, level, httpStatus } = event.detail;
    if (isolationKey != this.#connection?.isolationKey) {
      // This error does not concern our current connection.
      // This could be due to an old request after a token refresh.
      return null;
    }

    if (httpStatus === 401 || httpStatus === 407 || httpStatus == 403) {
      if (level == "error" || this.#pass?.shouldRotate()) {
        // If this is a visible top-level error force a rotation
        return this.rotateProxyPass();
      }
    }
    // Envoy returns a 401 if the token is rejected
    // So for now as we only care about rotating tokens we can exit here.
    return null;
  }

  updateState() {
    // State must remain as error until the connection is stopped.
    if (this.#state === IPPProxyStates.ERROR && this.#connection?.active) {
      this.#setState(IPPProxyStates.ERROR);
      return;
    }

    this.#errorType = null;

    if (lazy.IPProtectionService.state !== lazy.IPProtectionStates.READY) {
      this.#setState(IPPProxyStates.NOT_READY);
      return;
    }

    if (this.#usage?.quotaExhausted) {
      this.#setState(IPPProxyStates.PAUSED);
      return;
    }

    // State must remain active if the connection is active.
    if (this.#connection?.active) {
      this.#setState(IPPProxyStates.ACTIVE);
      return;
    }

    this.#setState(IPPProxyStates.READY);
  }

  /**
   * Helper to update the state after an error.
   *
   * @param {string} error - the error message that occurred.
   */
  #setErrorState(error) {
    this.#rotation?.controller.abort();

    this.#errorType =
      typeof error === "string" ? ERRORS.from(error) : ERRORS.GENERIC;
    if (this.#state === IPPProxyStates.ACTIVE) {
      // If the proxy is active, switch to the error state.
      // Stop will need to be called to move out of the error state.
      this.#setState(IPPProxyStates.ERROR);
    } else {
      // Otherwise, update to the previous state.
      this.updateState();
    }

    lazy.logConsole.error(error);
    Glean.ipprotection.error.record({ source: "ProxyManager" });
  }

  /**
   * @param {import("./GuardianTypes.sys.mjs").ProxyUsage } usage
   */
  #setUsage(usage) {
    if (usage.unlimited && this.#usage?.equals(usage)) {
      return;
    }
    this.#usage = usage;

    if (!usage.unlimited) {
      const now = Temporal.Now.instant();
      const daysUntilReset = now.until(usage.reset).total("days");
      lazy.logConsole.debug("ProxyUsage:", {
        usage: `${usage.remaining} / ${usage.max}`,
        resetsIn: `${daysUntilReset.toFixed(1)} days`,
      });
      this.#scheduleUsageCheck(usage);
    }

    this.dispatchEvent(
      new CustomEvent("IPPProxyManager:UsageChanged", {
        bubbles: true,
        composed: true,
        detail: {
          usage,
        },
      })
    );
  }

  #scheduleUsageCheck(usage) {
    if (this.#usageRefreshAbortController) {
      this.#usageRefreshAbortController.abort();
      this.#usageRefreshAbortController = null;
    }
    this.#usageRefreshAbortController = new AbortController();
    scheduleCallback(
      async () => {
        await this.refreshUsage();
      },
      usage.reset,
      this.#usageRefreshAbortController.signal
    );
  }

  /**
   * @param {IPPProxyState} state
   */
  #setState(state) {
    if (state === this.#state) {
      return;
    }

    this.#state = state;

    this.dispatchEvent(
      new CustomEvent("IPPProxyManager:StateChanged", {
        bubbles: true,
        composed: true,
        detail: {
          state,
        },
      })
    );
  }
}

const IPPProxyManager = new IPPProxyManagerSingleton();

export { IPPProxyManager };
