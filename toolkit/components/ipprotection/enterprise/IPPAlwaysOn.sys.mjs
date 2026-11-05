/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { PrivateBrowsingUtils } from "resource://gre/modules/PrivateBrowsingUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  IPProtectionServerlist:
    "moz-src:///toolkit/components/ipprotection/IPProtectionServerlist.sys.mjs",
  IPPProxyManager:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPPProxyStates:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  IPProtectionStates:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () =>
  console.createInstance({
    logModule: "IPP_AlwaysOn",
    maxLogLevel: Services.prefs.getBoolPref("browser.ipProtection.log", false)
      ? "Debug"
      : "Warn",
  })
);

const RESTART_BASE_DELAY_MS = 1000;
const RESTART_MAX_DELAY_MS = 30000;

/**
 * Keeps the proxy connection alive on enterprise builds where the
 * AccessConnector policy is active. Unlike IPPAutoStart, this class:
 *
 *  - Recovers from ERROR states by stopping and requesting a restart.
 *  - Restarts when the proxy stops unexpectedly.
 *  - Restarts are deferred and backed off so that repeated failures don't spin the main thread.
 *  - Switches to a new server when the server list is updated.
 *
 * Because this is policy-driven there is no user-facing toggle; the proxy
 * runs whenever the service is ready and the policy is set.
 */
class IPPAlwaysOnSingleton {
  #initialized = false;
  #shouldBeRunning = false;
  #startController = null;
  #restartDelayMs = 0;

  constructor() {
    this.handleServiceEvent = this.#handleServiceEvent.bind(this);
    this.handleProxyEvent = this.#handleProxyEvent.bind(this);
    this.handleServerlistEvent = this.#handleServerlistEvent.bind(this);
  }

  get alwaysOnEnabled() {
    return !!Services.policies.getActivePolicies()?.AccessConnector;
  }

  init() {
    if (this.#initialized || !this.alwaysOnEnabled) {
      lazy.logConsole.debug(
        "init() skipped — initialized:",
        this.#initialized,
        "alwaysOnEnabled:",
        this.alwaysOnEnabled
      );
      return;
    }
    lazy.logConsole.info("Initialized");
    this.#initialized = true;

    lazy.IPProtectionService.addEventListener(
      "IPProtectionService:StateChanged",
      this.handleServiceEvent
    );
    lazy.IPPProxyManager.addEventListener(
      "IPPProxyManager:StateChanged",
      this.handleProxyEvent
    );
    lazy.IPProtectionServerlist.addEventListener(
      "IPProtectionServerlist:ListChanged",
      this.handleServerlistEvent
    );

    // Always-on means always, so ask now to hold traffic for startup. Direct
    // rather than #requestStart(), whose backoff defers through a timer.
    this.#shouldBeRunning = true;
    lazy.IPPProxyManager.start(
      false,
      PrivateBrowsingUtils.permanentPrivateBrowsing
    );
  }

  initOnStartupCompleted() {}

  uninit() {
    if (!this.#initialized) {
      return;
    }
    this.#initialized = false;
    this.#shouldBeRunning = false;
    this.#cancelStartRequest();

    lazy.IPProtectionService.removeEventListener(
      "IPProtectionService:StateChanged",
      this.handleServiceEvent
    );
    lazy.IPPProxyManager.removeEventListener(
      "IPPProxyManager:StateChanged",
      this.handleProxyEvent
    );
    lazy.IPProtectionServerlist.removeEventListener(
      "IPProtectionServerlist:ListChanged",
      this.handleServerlistEvent
    );
  }

  /**
   * Returns true if a start request would be blocked by the current state of
   * the service, proxy, or server list.
   */
  #isStartBlocked() {
    if (lazy.IPPProxyManager.state === lazy.IPPProxyStates.ACTIVE) {
      return true;
    }
    return !lazy.IPProtectionServerlist.hasList;
  }

  /**
   * Requests a start of the proxy, with exponential backoff on repeated failures.
   *
   * @param {boolean} [resetBackoff]
   *   Cancel any live request and reschedule immediately with the base delay.
   */
  async #requestStart(resetBackoff = false) {
    if (resetBackoff) {
      this.#cancelStartRequest();
    }
    if (this.#startController || this.#isStartBlocked()) {
      return;
    }

    const controller = new AbortController();
    this.#startController = controller;
    const { signal } = controller;

    const delay = this.#restartDelayMs;
    this.#restartDelayMs = Math.min(
      this.#restartDelayMs ? this.#restartDelayMs * 2 : RESTART_BASE_DELAY_MS,
      RESTART_MAX_DELAY_MS
    );

    try {
      await this.#wait(delay, signal);
      if (signal.aborted || !this.#shouldBeRunning || !this.alwaysOnEnabled) {
        return;
      }
      if (this.#isStartBlocked()) {
        return;
      }
      lazy.logConsole.info("Starting proxy");
      await lazy.IPPProxyManager.start(
        false,
        PrivateBrowsingUtils.permanentPrivateBrowsing
      );
    } catch (e) {
      lazy.logConsole.error("Start request failed:", e);
    } finally {
      if (this.#startController === controller) {
        this.#startController = null;
      }
    }
  }

  /**
   * Releases any scheduled or in-flight start request.
   */
  #abortStartRequest() {
    if (this.#startController) {
      this.#startController.abort();
      this.#startController = null;
    }
  }

  /**
   * Cancels any scheduled or in-flight start request and resets the backoff delay.
   */
  #cancelStartRequest() {
    this.#abortStartRequest();
    this.#restartDelayMs = 0;
  }

  /**
   * Resolves after `ms` milliseconds, or immediately once `signal` aborts.
   *
   * @param {number} ms
   * @param {AbortSignal} signal
   * @returns {Promise<void>}
   */
  #wait(ms, signal) {
    const { promise, resolve } = Promise.withResolvers();
    const timer = lazy.setTimeout(resolve, ms);
    signal.addEventListener(
      "abort",
      () => {
        lazy.clearTimeout(timer);
        resolve();
      },
      { once: true }
    );
    return promise;
  }

  /**
   * Stops the proxy and, once it settles, requests a fresh start.
   *
   * @param {boolean} [resetBackoff] Reset the backoff delay to the base value.
   */
  async #stopThenRestart(resetBackoff = false) {
    try {
      await lazy.IPPProxyManager.stop(false);
    } catch (e) {
      lazy.logConsole.error("Failed to stop proxy:", e);
      return;
    }
    if (this.#shouldBeRunning && this.alwaysOnEnabled) {
      this.#requestStart(resetBackoff);
    }
  }

  #handleServiceEvent() {
    const serviceState = lazy.IPProtectionService.state;
    switch (serviceState) {
      case lazy.IPProtectionStates.UNINITIALIZED:
      case lazy.IPProtectionStates.UNAVAILABLE:
      case lazy.IPProtectionStates.UNAUTHENTICATED:
        this.#shouldBeRunning = false;
        this.#abortStartRequest();
        break;

      case lazy.IPProtectionStates.READY:
        this.#shouldBeRunning = true;
        this.#requestStart(true);
        break;

      default:
        break;
    }
  }

  #handleProxyEvent() {
    // alwaysOnEnabled flips to false synchronously when the policy is removed,
    // before uninit() runs. Bail out so we don't restart in response to the
    // ACTIVE->READY transition produced by teardown.
    if (!this.#shouldBeRunning || !this.alwaysOnEnabled) {
      return;
    }

    switch (lazy.IPPProxyManager.state) {
      case lazy.IPPProxyStates.ACTIVE:
        this.#cancelStartRequest();
        break;

      case lazy.IPPProxyStates.READY:
        this.#abortStartRequest();
        this.#requestStart();
        break;

      case lazy.IPPProxyStates.ERROR:
        this.#abortStartRequest();
        // Repeated failures back off; don't reset the delay.
        this.#stopThenRestart();
        break;

      default:
        break;
    }
  }

  #handleServerlistEvent() {
    if (!this.alwaysOnEnabled) {
      return;
    }
    if (!lazy.IPProtectionServerlist.hasList) {
      // Serverlist cleared (e.g. policy removed); stop any active connection.
      const state = lazy.IPPProxyManager.state;
      if (
        state === lazy.IPPProxyStates.ACTIVE ||
        state === lazy.IPPProxyStates.ERROR
      ) {
        lazy.IPPProxyManager.stop(false);
      }
      return;
    }
    const state = lazy.IPPProxyManager.state;
    switch (state) {
      case lazy.IPPProxyStates.ACTIVE: {
        // Hot-swap without dropping the connection.
        lazy.logConsole.debug("Switching to updated server");
        const { error } = lazy.IPPProxyManager.switch();
        if (error) {
          // Fresh server list, reset backoff so the reconnect isn't delayed.
          this.#stopThenRestart(true);
        }
        break;
      }

      case lazy.IPPProxyStates.ERROR:
        // A fresh server list may resolve the error; reset backoff to retry promptly.
        if (this.#shouldBeRunning) {
          this.#stopThenRestart(true);
        }
        break;

      case lazy.IPPProxyStates.READY:
        if (this.#shouldBeRunning) {
          this.#requestStart(true);
        }
        break;

      default:
        break;
    }
  }
}

const IPPAlwaysOn = new IPPAlwaysOnSingleton();

export { IPPAlwaysOn, IPPAlwaysOnSingleton };
