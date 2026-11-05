/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

const LOG_PREF = "browser.ipProtection.log";

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "IPP_NetworkUtils",
    maxLogLevel: Services.prefs.getBoolPref(LOG_PREF, false) ? "Debug" : "Warn",
  });
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "CaptivePortalService",
  "@mozilla.org/network/captive-portal-service;1",
  Ci.nsICaptivePortalService
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gNetworkLinkService",
  "@mozilla.org/network/network-link-service;1",
  Ci.nsINetworkLinkService
);

/**
 * Provides network connectivity detection utilities for IP Protection.
 *
 * This class implements a comprehensive offline check using:
 * - Services.io.offline (user-set offline mode)
 * - CaptivePortalService (connected but captive portal blocking internet)
 * - NetworkLinkService (physical network link status)
 */
export const IPPNetworkUtils = {
  /**
   * Checks if the browser is currently offline or unable to access the internet.
   *
   * @returns {boolean}
   *   True if offline status, false otherwise.
   */
  get isOffline() {
    try {
      return (
        Services.io.offline ||
        lazy.CaptivePortalService.state ==
          lazy.CaptivePortalService.LOCKED_PORTAL ||
        !lazy.gNetworkLinkService.isLinkUp
      );
    } catch (e) {
      lazy.logConsole.warn("Could not determine network status.", e);
    }
    return false;
  },
};
