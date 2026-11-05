/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "ProxyService",
  "@mozilla.org/network/protocol-proxy-service;1",
  Ci.nsIProtocolProxyService
);

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "SponsorProtection",
    maxLogLevel: Services.prefs.getBoolPref(
      "browser.newtabpage.sponsor-protection.debug",
      false
    )
      ? "Debug"
      : "Warn",
  });
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "SPONSOR_PROTECTION_ENABLED",
  "browser.newtabpage.sponsor-protection.enabled",
  false
);

const HTTP_STOP_REQUEST_TOPIC = "http-on-stop-request";
const BYTES_PER_KB = 1024;

/**
 * This class tracks a list of <browser> elements that have done a navigation
 * by way of a sponsored link from New Tab. The goal is to eventually apply
 * client protections on these <browser> elements such that network traffic is
 * forwarded through an HTTP CONNECT or MASQUE relay, thus hiding the client's
 * real IP address from the advertiser site. A less unique UA string will also
 * be assigned to <browser> elements under protection.
 */
export class _SponsorProtection {
  #protectedBrowsers = new WeakSet();
  #observerAndFilterAdded = false;
  #debugEnabled = false;

  constructor() {
    this.#debugEnabled = Services.prefs.getBoolPref(
      "browser.newtabpage.sponsor-protection.debug",
      false
    );
  }

  /**
   * True if the SponsorProtection mechanism is enabled.
   */
  get enabled() {
    return lazy.SPONSOR_PROTECTION_ENABLED;
  }

  /**
   * True if the debug mode for SponsorProtection was enabled upon construction.
   * Debug mode adds a decoration to the tab hover preview to help identify
   * protected browsers, and also emits logging to the console.
   */
  get debugEnabled() {
    return this.#debugEnabled;
  }

  /**
   * Registers a <browser> so that sponsor protection is applied for
   * subsequent network connections from that <browser>.
   *
   * @param {Browser} browser
   *   The <browser> to have sponsor protected applied.
   */
  addProtectedBrowser(browser) {
    if (!this.enabled) {
      return;
    }

    if (!this.#observerAndFilterAdded) {
      this.#addObserverAndChannelFilter();
    }

    this.#protectedBrowsers.add(browser.permanentKey);
    lazy.logConsole.debug("Registering browser for sponsor protection");

    // TODO: This is where the clamped UA string will be applied to this
    // browser.
  }

  /**
   * Unregisters a <browser> so that sponsor protection is no longer
   * applied for subsequent network connections from that <browser>.
   * This is a no-op if the <browser> was not actually being protected.
   *
   * @param {Browser} browser
   *   The <browser> to have sponsor protected removed.
   */
  removeProtectedBrowser(browser) {
    this.#protectedBrowsers.delete(browser.permanentKey);
    lazy.logConsole.debug("Unregistering browser for sponsor protection");

    // TODO: This is where we remove the clamped UA string from this browser.
  }

  /**
   * Returns true if the <browser> is having sponsor protection applied.
   *
   * @param {Browser} browser
   * @returns {boolean}
   */
  isProtectedBrowser(browser) {
    return this.#protectedBrowsers.has(browser.permanentKey);
  }

  /**
   * Sets up the HTTP_STOP_REQUEST_TOPIC observer and proxy channel filter when
   * the number of protected browsers in the WeakSet goes from 0 to 1.
   */
  #addObserverAndChannelFilter() {
    Services.obs.addObserver(this, HTTP_STOP_REQUEST_TOPIC);

    lazy.ProxyService.registerChannelFilter(this, 0);
    this.#observerAndFilterAdded = true;
    lazy.logConsole.debug("Added observer and channel filter.");
  }

  /**
   * Removes the HTTP_STOP_REQUEST_TOPIC observer and proxy channel filter when
   * the number of protected browsers in the WeakSet goes to 0.
   */
  #removeObserverAndChannelFilter() {
    Services.obs.removeObserver(this, HTTP_STOP_REQUEST_TOPIC);

    lazy.ProxyService.unregisterChannelFilter(this);
    this.#observerAndFilterAdded = false;
    lazy.logConsole.debug("Removed observer and channel filter.");
  }

  /* nsIObserver */

  /**
   * Observes the HTTP_STOP_REQUEST_TOPIC observer notification and, if the
   * associated channel comes from a protected browser, records the request
   * and response sizes to telemetry.
   *
   * This observer is also used to determine if there are remaining protected
   * browsers - and if not, to unregister the observer and channel filter.
   *
   * @param {nsIChannel} subject
   *   For HTTP_STOP_REQUEST_TOPIC, this should be an nsIChannel.
   * @param {string} topic
   * @param {string} _data
   */
  observe(subject, topic, _data) {
    if (topic != HTTP_STOP_REQUEST_TOPIC) {
      return;
    }

    // If all the <browser> elements have gone away from our WeakSet, at this
    // point we can go ahead and get rid of our observer and filter.
    if (
      !ChromeUtils.nondeterministicGetWeakSetKeys(this.#protectedBrowsers)
        .length
    ) {
      this.#removeObserverAndChannelFilter();
      return;
    }

    if (!(subject instanceof Ci.nsIHttpChannel)) {
      return;
    }

    let channel = subject;
    const { browsingContext } = channel.loadInfo;
    let browser = browsingContext?.top.embedderElement;
    if (!browser || !this.#protectedBrowsers.has(browser.permanentKey)) {
      return;
    }

    // requestSize includes the request headers and payload
    const requestSize = channel.requestSize;

    // transferSize includes the response headers and payload
    const responseSize = channel.transferSize;

    Glean.newtab.sponsNavTrafficSent.accumulate(
      Math.round(requestSize / BYTES_PER_KB)
    );
    Glean.newtab.sponsNavTrafficRecvd.accumulate(
      Math.round(responseSize / BYTES_PER_KB)
    );

    lazy.logConsole.debug(
      `Channel for ${browser.currentURI.spec} (${channel.URI.spec}) - sent: ${requestSize} recv'd: ${responseSize}`
    );
  }

  /* nsIProtocolProxyChannelFilter */

  /**
   * Checks a created nsIChannel to see if it qualifies for proxying. If it
   * does, proxy configuration appropriate for this client is applied.
   *
   * @param {nsIChannel} channel
   * @param {nsIProxyInfo} proxyInfo
   * @param {nsIProxyProtocolFilterResult} callback
   */
  applyFilter(channel, proxyInfo, callback) {
    const { browsingContext } = channel.loadInfo;
    let browser = browsingContext?.top.embedderElement;
    if (!browser || !this.#protectedBrowsers.has(browser)) {
      callback.onProxyFilterResult(proxyInfo);
      return;
    }

    // This is where proxy information, if we have any, will be applied for
    // the connection. For now however, we're not proxying anything, so just
    // fallthrough to the default behaviour.
    callback.onProxyFilterResult(proxyInfo);
  }

  QueryInterface = ChromeUtils.generateQI([
    Ci.nsIObserver,
    Ci.nsIProtocolProxyChannelFilter,
  ]);
}

export const SponsorProtection = new _SponsorProtection();
