/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  FXA_PWDMGR_HOST: "resource://gre/modules/FxAccountsCommon.sys.mjs",
  FXA_PWDMGR_REALM: "resource://gre/modules/FxAccountsCommon.sys.mjs",
  LoginBreaches:
    "moz-src:///browser/components/aboutlogins/LoginBreaches.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  PrivacyMetricsService:
    "moz-src:///browser/components/protections/PrivacyMetricsService.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "fxAccounts", () => {
  return ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton();
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "TrackingDBService",
  "@mozilla.org/tracking-db-service;1",
  Ci.nsITrackingDBService
);

let idToTextMap = new Map([
  [Ci.nsITrackingDBService.TRACKERS_ID, "tracker"],
  [Ci.nsITrackingDBService.TRACKING_COOKIES_ID, "cookie"],
  [Ci.nsITrackingDBService.CRYPTOMINERS_ID, "cryptominer"],
  [Ci.nsITrackingDBService.FINGERPRINTERS_ID, "fingerprinter"],
  // We map the suspicious fingerprinter to fingerprinter category to aggregate
  // the number.
  [Ci.nsITrackingDBService.SUSPICIOUS_FINGERPRINTERS_ID, "fingerprinter"],
  [Ci.nsITrackingDBService.SOCIAL_ID, "social"],
]);

const MONITOR_API_ENDPOINT = Services.urlFormatter.formatURLPref(
  "browser.contentblocking.report.endpoint_url"
);

// The ID of the vpn subscription, if we see this ID attached to a user's account then they have subscribed to vpn.
const VPN_SUB_ID = Services.prefs.getStringPref(
  "browser.contentblocking.report.vpn_sub_id"
);

// Error messages
const INVALID_OAUTH_TOKEN = "Invalid OAuth token";
const USER_UNSUBSCRIBED_TO_MONITOR = "User is not subscribed to Monitor";
const SERVICE_UNAVAILABLE = "Service unavailable";
const UNEXPECTED_RESPONSE = "Unexpected response";
const UNKNOWN_ERROR = "Unknown error";

// Valid response info for successful Monitor data
const MONITOR_RESPONSE_PROPS = [
  "monitoredEmails",
  "numBreaches",
  "passwords",
  "numBreachesResolved",
  "passwordsResolved",
];

let gTestOverride = null;
let monitorResponse = null;
let entrypoint = "direct";

export class AboutProtectionsParent extends JSWindowActorParent {
  constructor() {
    super();
  }

  // Some tests wish to override certain functions with ones that mostly do nothing.
  static setTestOverride(callback) {
    gTestOverride = callback;
  }

  /**
   * Fetches and validates data from the Monitor endpoint. If successful, then return
   * expected data. Otherwise, throw the appropriate error depending on the status code.
   *
   * @return valid data from endpoint.
   */
  async fetchUserBreachStats(token) {
    if (monitorResponse && monitorResponse.timestamp) {
      var timeDiff = Date.now() - monitorResponse.timestamp;
      let oneDayInMS = 24 * 60 * 60 * 1000;
      if (timeDiff >= oneDayInMS) {
        monitorResponse = null;
      } else {
        return monitorResponse;
      }
    }

    // Make the request
    const headers = new Headers();
    headers.append("Authorization", `Bearer ${token}`);
    const request = new Request(MONITOR_API_ENDPOINT, { headers });
    const response = await fetch(request);

    if (response.ok) {
      // Validate the shape of the response is what we're expecting.
      const json = await response.json();

      // Make sure that we're getting the expected data.
      let isValid = null;
      for (let prop in json) {
        isValid = MONITOR_RESPONSE_PROPS.includes(prop);

        if (!isValid) {
          break;
        }
      }

      monitorResponse = isValid ? json : new Error(UNEXPECTED_RESPONSE);
      if (isValid) {
        monitorResponse.timestamp = Date.now();
      }
    } else {
      // Check the reason for the error
      switch (response.status) {
        case 400:
        case 401:
          monitorResponse = new Error(INVALID_OAUTH_TOKEN);
          break;
        case 404:
          monitorResponse = new Error(USER_UNSUBSCRIBED_TO_MONITOR);
          break;
        case 503:
          monitorResponse = new Error(SERVICE_UNAVAILABLE);
          break;
        default:
          monitorResponse = new Error(UNKNOWN_ERROR);
          break;
      }
    }

    if (monitorResponse instanceof Error) {
      throw monitorResponse;
    }
    return monitorResponse;
  }

  /**
   * Retrieves login data for the user.
   *
   * @return {{numLogins: number, potentiallyBreachedLogins: number, mobileDeviceConnected: boolean }}
   */
  async getLoginData() {
    if (gTestOverride && "getLoginData" in gTestOverride) {
      return gTestOverride.getLoginData();
    }

    try {
      if (await lazy.fxAccounts.getSignedInUser()) {
        await lazy.fxAccounts.device.refreshDeviceList();
      }
    } catch (e) {
      console.error("There was an error fetching login data: ", e.message);
    }

    const userFacingLogins =
      (await Services.logins.countLoginsAsync("", "", "")) -
      (await Services.logins.countLoginsAsync(
        lazy.FXA_PWDMGR_HOST,
        null,
        lazy.FXA_PWDMGR_REALM
      ));

    let potentiallyBreachedLogins = null;
    // Get the stats for number of potentially breached Lockwise passwords
    // if the Primary Password isn't locked.
    if (userFacingLogins && Services.logins.isLoggedIn) {
      const logins = await lazy.LoginHelper.getAllUserFacingLogins();
      potentiallyBreachedLogins =
        await lazy.LoginBreaches.getPotentialBreachesByLoginGUID(logins);
    }

    let mobileDeviceConnected =
      lazy.fxAccounts.device.recentDeviceList &&
      lazy.fxAccounts.device.recentDeviceList.filter(
        device => device.type == "mobile"
      ).length;

    return {
      numLogins: userFacingLogins,
      potentiallyBreachedLogins: potentiallyBreachedLogins
        ? potentiallyBreachedLogins.size
        : 0,
      mobileDeviceConnected,
    };
  }

  /**
   * @typedef {object} ProtectionsMonitorData
   * @property {number} monitoredEmails
   * @property {number} numBreaches
   * @property {number} passwords
   * @property {?string} userEmail
   * @property {boolean} error
   */

  /**
   * Retrieves monitor data for the user.
   *
   * @return {ProtectionsMonitorData}
   */
  async getMonitorData() {
    if (gTestOverride && "getMonitorData" in gTestOverride) {
      monitorResponse = gTestOverride.getMonitorData();
      monitorResponse.timestamp = Date.now();
      // In a test, expect this to not fetch from the monitor endpoint due to the timestamp guaranteeing we use the cache.
      monitorResponse = await this.fetchUserBreachStats();
      return monitorResponse;
    }

    let monitorData = {};
    let userEmail = null;
    let token = await this.getMonitorScopedOAuthToken();

    try {
      if (token) {
        monitorData = await this.fetchUserBreachStats(token);

        // Send back user's email so the protections report can direct them to the proper
        // OAuth flow on Monitor.
        const { email } = await lazy.fxAccounts.getSignedInUser();
        userEmail = email;
      } else {
        // If no account exists, then the user is not logged in with an fxAccount.
        monitorData = {
          errorMessage: "No account",
        };
      }
    } catch (e) {
      console.error(e.message);
      monitorData.errorMessage = e.message;

      // If the user's OAuth token is invalid, we clear the cached token and refetch
      // again. If OAuth token is invalid after the second fetch, then the monitor UI
      // will simply show the "no logins" UI version.
      if (e.message === INVALID_OAUTH_TOKEN) {
        await lazy.fxAccounts.removeCachedOAuthToken({ token });
        token = await this.getMonitorScopedOAuthToken();

        try {
          monitorData = await this.fetchUserBreachStats(token);
        } catch (_) {
          console.error(e.message);
        }
      } else if (e.message === USER_UNSUBSCRIBED_TO_MONITOR) {
        // Send back user's email so the protections report can direct them to the proper
        // OAuth flow on Monitor.
        const { email } = await lazy.fxAccounts.getSignedInUser();
        userEmail = email;
      } else {
        monitorData.errorMessage = e.message || "An error ocurred.";
      }
    }

    return {
      ...monitorData,
      userEmail,
      error: !!monitorData.errorMessage,
    };
  }

  async getMonitorScopedOAuthToken() {
    let token = null;

    try {
      token = await lazy.fxAccounts.getOAuthToken({ service: "monitor" });
    } catch (e) {
      console.error(
        "There was an error fetching the user's token: ",
        e.message
      );
    }

    return token;
  }

  async VPNSubStatus() {
    // For testing, set vpn sub status manually
    if (gTestOverride && "vpnOverrides" in gTestOverride) {
      return gTestOverride.vpnOverrides();
    }

    let subscriptions;
    try {
      subscriptions = await lazy.fxAccounts.activeSubscriptions();
    } catch (e) {
      console.error(
        "There was an error fetching the user's subscriptions: ",
        e.message
      );
      // there was an error, assume user is not subscribed to VPN
      return false;
    }
    return subscriptions.some(sub => sub.subscriptionId == VPN_SUB_ID);
  }

  async receiveMessage(aMessage) {
    let win = this.browsingContext.top.embedderElement.documentGlobal;
    switch (aMessage.name) {
      case "OpenAboutLogins":
        lazy.LoginHelper.openPasswordManager(win, {
          entryPoint: "Aboutprotections",
        });
        break;
      case "OpenContentBlockingPreferences":
        win.openPreferences("privacy-trackingprotection", {
          origin: "about-protections",
        });
        break;
      case "OpenSyncPreferences":
        win.openTrustedLinkIn("about:preferences#sync", "tab");
        break;
      case "FetchContentBlockingEvents": {
        let dataToSend = {};
        let displayNames = new Services.intl.DisplayNames(undefined, {
          type: "weekday",
          style: "abbreviated",
          calendar: "gregory",
        });

        // Weekdays starting Sunday (7) to Saturday (6).
        let weekdays = [7, 1, 2, 3, 4, 5, 6].map(day => displayNames.of(day));
        dataToSend.weekdays = weekdays;

        if (lazy.PrivateBrowsingUtils.isWindowPrivate(win)) {
          dataToSend.isPrivate = true;
          return dataToSend;
        }
        let sumEvents = await lazy.TrackingDBService.sumAllEvents();
        let earliestDate =
          await lazy.TrackingDBService.getEarliestRecordedDate();
        let eventsByDate = await lazy.TrackingDBService.getEventsByDateRange(
          aMessage.data.from,
          aMessage.data.to
        );
        let largest = 0;

        for (let result of eventsByDate) {
          let count = result.getResultByName("count");
          let type = result.getResultByName("type");
          let timestamp = result.getResultByName("timestamp");
          let typeStr = idToTextMap.get(type);
          dataToSend[timestamp] = dataToSend[timestamp] ?? { total: 0 };
          let currentCnt = dataToSend[timestamp][typeStr] ?? 0;
          currentCnt += count;
          dataToSend[timestamp][typeStr] = currentCnt;
          dataToSend[timestamp].total += count;
          // Record the largest amount of tracking events found per day,
          // to create the tallest column on the graph and compare other days to.
          if (largest < dataToSend[timestamp].total) {
            largest = dataToSend[timestamp].total;
          }
        }
        dataToSend.largest = largest;
        dataToSend.earliestDate = earliestDate;
        dataToSend.sumEvents = sumEvents;

        return dataToSend;
      }

      case "FetchMonitorData":
        return this.getMonitorData();

      case "FetchUserLoginsData":
        return this.getLoginData();

      case "ClearMonitorCache":
        monitorResponse = null;
        break;

      case "RecordEntryPoint":
        entrypoint = aMessage.data.entrypoint;
        break;

      case "FetchEntryPoint": {
        // The entrypoint only applies to the load that follows the URL
        // rewrite in protections.mjs; later loads are direct.
        let value = entrypoint;
        entrypoint = "direct";
        return value;
      }

      case "FetchVPNSubStatus":
        return this.VPNSubStatus();

      case "FetchShowVPNCard":
        return lazy.BrowserUtils.shouldShowVPNPromo();

      case "FetchPrivacyMetrics":
        if (lazy.PrivateBrowsingUtils.isWindowPrivate(win)) {
          return { isPrivate: true };
        }
        return lazy.PrivacyMetricsService.getWeeklyStats();
    }

    return undefined;
  }
}
