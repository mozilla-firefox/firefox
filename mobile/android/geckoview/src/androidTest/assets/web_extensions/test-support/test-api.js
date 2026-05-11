/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* globals Services */

const { E10SUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/E10SUtils.sys.mjs"
);
const { Preferences } = ChromeUtils.importESModule(
  "resource://gre/modules/Preferences.sys.mjs"
);

this.test = class extends ExtensionAPI {
  onStartup() {
    ChromeUtils.registerWindowActor("TestSupport", {
      child: {
        esModuleURI:
          "resource://android/assets/web_extensions/test-support/TestSupportChild.sys.mjs",
      },
      allFrames: true,
    });
    ChromeUtils.registerProcessActor("TestSupportProcess", {
      child: {
        esModuleURI:
          "resource://android/assets/web_extensions/test-support/TestSupportProcessChild.sys.mjs",
      },
    });
  }

  onShutdown(isAppShutdown) {
    if (isAppShutdown) {
      return;
    }
    ChromeUtils.unregisterWindowActor("TestSupport");
    ChromeUtils.unregisterProcessActor("TestSupportProcess");
  }

  getAPI(context) {
    /**
     * Helper function for getting window or process actors.
     *
     * @param tabId - id of the tab; required
     * @param actorName - a string; the name of the actor
     *   Default: "TestSupport" which is our test framework actor
     *   (you can still pass the second parameter when getting the TestSupport actor, for readability)
     *
     * @returns actor
     */
    function getActorForTab(tabId, actorName = "TestSupport") {
      const tab = context.extension.tabManager.get(tabId);
      const { browsingContext } = tab.browser;
      return browsingContext.currentWindowGlobal.getActor(actorName);
    }

    return {
      test: {
        /* Set prefs and returns set of saved prefs */
        async setPrefs(oldPrefs, newPrefs) {
          // Save old prefs
          Object.assign(
            oldPrefs,
            ...Object.keys(newPrefs)
              .filter(key => !(key in oldPrefs))
              .map(key => ({ [key]: Preferences.get(key, null) }))
          );

          // Set new prefs
          Preferences.set(newPrefs);
          return oldPrefs;
        },

        /* Restore prefs to old value. */
        async restorePrefs(oldPrefs) {
          for (const [name, value] of Object.entries(oldPrefs)) {
            if (value === null) {
              Preferences.reset(name);
            } else {
              Preferences.set(name, value);
            }
          }
        },

        /* Get pref values. */
        async getPrefs(prefs) {
          return Preferences.get(prefs);
        },

        /* Clears a given user preference. */
        async clearUserPref(pref) {
          Services.prefs.clearUserPref(pref);
        },
        /* Gets link color for a given selector. */
        async getLinkColor(tabId, selector) {
          return getActorForTab(tabId, "TestSupport").sendQuery(
            "GetLinkColor",
            { selector }
          );
        },

        async getRequestedLocales() {
          return Services.locale.requestedLocales;
        },

        async getPidForTab(tabId) {
          const tab = context.extension.tabManager.get(tabId);
          const pids = E10SUtils.getBrowserPids(tab.browser);
          return pids[0];
        },

        async waitForContentTransformsReceived(tabId) {
          return getActorForTab(tabId).sendQuery(
            "WaitForContentTransformsReceived"
          );
        },

        async getAllBrowserPids() {
          const pids = [];
          const processes = ChromeUtils.getAllDOMProcesses();
          for (const process of processes) {
            if (process.remoteType && process.remoteType.startsWith("web")) {
              pids.push(process.osPid);
            }
          }
          return pids;
        },

        async killContentProcess(pid) {
          const procs = ChromeUtils.getAllDOMProcesses();
          for (const proc of procs) {
            if (pid === proc.osPid) {
              proc
                .getActor("TestSupportProcess")
                .sendAsyncMessage("KillContentProcess");
            }
          }
        },

        async addHistogram(id, value) {
          return Services.telemetry.getHistogramById(id).add(value);
        },

        removeAllCertOverrides() {
          const overrideService = Cc[
            "@mozilla.org/security/certoverride;1"
          ].getService(Ci.nsICertOverrideService);
          overrideService.clearAllOverrides();
        },

        async setResolutionAndScaleTo(tabId, resolution) {
          return getActorForTab(tabId, "TestSupport").sendQuery(
            "SetResolutionAndScaleTo",
            {
              resolution,
            }
          );
        },

        async getActive(tabId) {
          const tab = context.extension.tabManager.get(tabId);
          return tab.browser.docShellIsActive;
        },

        async getProfilePath() {
          return PathUtils.profileDir;
        },

        async flushApzRepaints(tabId) {
          // TODO: Note that `waitUntilApzStable` in apz_test_utils.js does
          // flush APZ repaints in the parent process (i.e. calling
          // nsIDOMWindowUtils.flushApzRepaints for the parent process) before
          // flushApzRepaints is called for the target content document, if we
          // still meet intermittent failures, we might want to do it here as
          // well.
          await getActorForTab(tabId, "TestSupport").sendQuery(
            "FlushApzRepaints"
          );
        },

        async zoomToFocusedInput(tabId) {
          await getActorForTab(tabId, "TestSupport").sendQuery(
            "ZoomToFocusedInput"
          );
        },

        async promiseAllPaintsDone(tabId) {
          await getActorForTab(tabId, "TestSupport").sendQuery(
            "PromiseAllPaintsDone"
          );
        },

        async usingGpuProcess() {
          const gfxInfo = Cc["@mozilla.org/gfx/info;1"].getService(
            Ci.nsIGfxInfo
          );
          return gfxInfo.usingGPUProcess;
        },

        async killGpuProcess() {
          const gfxInfo = Cc["@mozilla.org/gfx/info;1"].getService(
            Ci.nsIGfxInfo
          );
          return gfxInfo.killGPUProcessForTests();
        },

        async crashGpuProcess() {
          const gfxInfo = Cc["@mozilla.org/gfx/info;1"].getService(
            Ci.nsIGfxInfo
          );
          return gfxInfo.crashGPUProcessForTests();
        },

        async clearHSTSState() {
          const sss = Cc["@mozilla.org/ssservice;1"].getService(
            Ci.nsISiteSecurityService
          );
          return sss.clearAll();
        },

        async isFissionRunning() {
          return Services.appinfo.fissionAutostart;
        },

        async triggerCookieBannerDetected(tabId) {
          const actor = getActorForTab(tabId, "CookieBanner");
          return actor.receiveMessage({
            name: "CookieBanner::DetectedBanner",
          });
        },

        async triggerCookieBannerHandled(tabId) {
          const actor = getActorForTab(tabId, "CookieBanner");
          return actor.receiveMessage({
            name: "CookieBanner::HandledBanner",
          });
        },

        async triggerTranslationsOffer(tabId) {
          const browser = context.extension.tabManager.get(tabId).browser;
          const { CustomEvent } = browser.documentGlobal;
          return browser.dispatchEvent(
            new CustomEvent("TranslationsParent:OfferTranslation", {
              bubbles: true,
            })
          );
        },

        async triggerLanguageStateChange(tabId, languageState) {
          const browser = context.extension.tabManager.get(tabId).browser;
          const { CustomEvent } = browser.documentGlobal;
          return browser.dispatchEvent(
            new CustomEvent("TranslationsParent:LanguageState", {
              bubbles: true,
              detail: languageState,
            })
          );
        },

        async setHandlingUserInput(tabId, handlingUserInput) {
          return getActorForTab(tabId, "TestSupport").sendQuery(
            "SetHandlingUserInput",
            { handlingUserInput }
          );
        },

        async getWebExtensionsSchemaPermissionNames(typeNames) {
          const { Schemas } = ChromeUtils.importESModule(
            "resource://gre/modules/Schemas.sys.mjs"
          );
          return Schemas.getPermissionNames(typeNames);
        },

        async teardownAlertsService() {
          const alertsService = Cc["@mozilla.org/alerts-service;1"].getService(
            Ci.nsIAlertsService
          );
          alertsService.teardown();
        },

        async notifyUserGestureActivation(tabId) {
          return getActorForTab(tabId, "TestSupport").sendQuery(
            "NotifyUserGestureActivation"
          );
        },

        /* Seeds the tracking protection database with the given content blocking log. */
        async saveTrackingDBEvents(logJson) {
          const trackingDBService = Cc[
            "@mozilla.org/tracking-db-service;1"
          ].getService(Ci.nsITrackingDBService);
          await trackingDBService.saveEvents(logJson);
        },

        /* Removes all entries from the tracking protection database. */
        async clearTrackingDB() {
          const trackingDBService = Cc[
            "@mozilla.org/tracking-db-service;1"
          ].getService(Ci.nsITrackingDBService);
          await trackingDBService.clearAll();
        },

        async addVirtualAuthenticator() {
          const webauthnService = Cc[
            "@mozilla.org/webauthn/service;1"
          ].getService(Ci.nsIWebAuthnService);
          return webauthnService.addVirtualAuthenticator(
            "ctap2_1",
            "internal",
            true,
            true,
            true,
            true
          );
        },

        async removeVirtualAuthenticator(authenticatorId) {
          const webauthnService = Cc[
            "@mozilla.org/webauthn/service;1"
          ].getService(Ci.nsIWebAuthnService);
          webauthnService.removeVirtualAuthenticator(authenticatorId);
        },
      },
    };
  }
};
