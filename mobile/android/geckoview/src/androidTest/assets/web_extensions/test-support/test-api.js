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
const { GeckoViewPictureInPictureController } = ChromeUtils.importESModule(
  "resource://gre/modules/GeckoViewPictureInPicture.sys.mjs"
);

this.test = class extends ExtensionAPI {
  onStartup() {
    ChromeUtils.registerWindowActor("TestSupport", {
      child: {
        esModuleURI:
          "resource://android/assets/web_extensions/test-support/TestSupportChild.sys.mjs",
      },
      allFrames: true,
      safeForUntrustedWebProcess: true,
    });
    ChromeUtils.registerProcessActor("TestSupportProcess", {
      child: {
        esModuleURI:
          "resource://android/assets/web_extensions/test-support/TestSupportProcessChild.sys.mjs",
      },
      safeForUntrustedWebProcess: true,
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

        /* Whether the video at `selector` is being cloned into another video element. */
        async isVideoCloning(tabId, selector) {
          return getActorForTab(tabId, "TestSupport").sendQuery(
            "IsVideoCloning",
            { selector }
          );
        },

        /* The message the video controls overlay shows over the video at `selector`, if any. */
        async getVideoStatusMessage(tabId, selector) {
          return getActorForTab(tabId, "TestSupport").sendQuery(
            "GetVideoStatusMessage",
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

        async getBrowsingContextGroupIdForTab(tabId) {
          const tab = context.extension.tabManager.get(tabId);
          return tab.browser.browsingContext.group.id;
        },

        async getRemoteTypeForTab(tabId) {
          const tab = context.extension.tabManager.get(tabId);
          return tab.browser.remoteType;
        },

        async mintPictureInPicturePlacementToken(tabId) {
          const tab = context.extension.tabManager.get(tabId);
          return GeckoViewPictureInPictureController.mintPlacementToken(
            tab.browser.browsingContext.group.id,
            tab.browser.remoteType
          );
        },

        async waitForContentTransformsReceived(tabId) {
          const { browsingContext } =
            context.extension.tabManager.get(tabId).browser;
          return Promise.all(
            browsingContext
              .getAllBrowsingContextsInSubtree()
              .map(bc => bc.currentWindowGlobal)
              .filter(windowGlobal => windowGlobal?.isProcessRoot)
              .map(windowGlobal =>
                windowGlobal
                  .getActor("TestSupport")
                  .sendQuery("WaitForContentTransformsReceived")
              )
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

        async showPicker(tabId, selector) {
          return getActorForTab(tabId, "TestSupport").sendQuery("ShowPicker", {
            selector,
          });
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

        /*
         * Seed the IP protection test auth provider (selected by setting the
         * "toolkit.ipProtection.android.authProvider" pref to "test") with a
         * faked Guardian backend, mirroring the desktop xpcshell setupStubs.
         * `options.entitlement` overrides the default test entitlement fields.
         */
        async setupIPPAuthProvider(options = {}) {
          const { Entitlement, ProxyPass, ProxyUsage } =
            ChromeUtils.importESModule(
              "moz-src:///toolkit/components/ipprotection/GuardianTypes.sys.mjs"
            );
          const { IPPDummyAuthProvider } = ChromeUtils.importESModule(
            "moz-src:///toolkit/components/ipprotection/tests/IPPDummyAuthProvider.sys.mjs"
          );
          const signedIn = options.signedIn ?? true;
          const entitlement = new Entitlement({
            autostart: false,
            created_at: "2023-01-01T12:00:00.000Z",
            limited_bandwidth: false,
            location_controls: false,
            subscribed: false,
            uid: 42,
            website_inclusion: false,
            maxBytes: "0",
            ...(options.entitlement ?? {}),
          });
          IPPDummyAuthProvider.simulateSignIn(signedIn);
          IPPDummyAuthProvider.setEntitlement(entitlement, { silent: true });
          IPPDummyAuthProvider.setGetEntitlementResponse({ entitlement });
          IPPDummyAuthProvider.setEnrollResponse({
            isEnrolledAndEntitled: true,
            entitlement,
          });
          IPPDummyAuthProvider.setProxyPassError(null);
          // The JWT proxy-pass token is minted in background.js, where btoa is
          // available (the parent sandbox only exposes ChromeUtils).
          if (options.proxyPassToken) {
            const usage = new ProxyUsage(
              "5368709120",
              "4294967296",
              "3026-02-01T00:00:00.000Z"
            );
            IPPDummyAuthProvider.setProxyPass({
              status: 200,
              error: undefined,
              pass: new ProxyPass(options.proxyPassToken),
              usage,
            });
            IPPDummyAuthProvider.setProxyUsage(usage);
          }
        },

        /*
         * Set what the test auth provider's fetchProxyUsage resolves to. When
         * `usage.unlimited` is true the byte fields are ignored (ProxyUsage
         * leaves them null). Pass null to clear.
         */
        async setIPPProxyUsage(usage) {
          const { ProxyUsage } = ChromeUtils.importESModule(
            "moz-src:///toolkit/components/ipprotection/GuardianTypes.sys.mjs"
          );
          const { IPPDummyAuthProvider } = ChromeUtils.importESModule(
            "moz-src:///toolkit/components/ipprotection/tests/IPPDummyAuthProvider.sys.mjs"
          );
          IPPDummyAuthProvider.setProxyUsage(
            usage
              ? new ProxyUsage(
                  usage.max ?? null,
                  usage.remaining ?? null,
                  usage.reset ?? null,
                  usage.unlimited ?? true
                )
              : null
          );
        },

        /*
         * Make the test auth provider's fetchProxyPass throw the given error
         * string (propagated verbatim to the activation error), or pass null to
         * restore the normal response path.
         */
        async setIPPProxyPassError(error) {
          const { IPPDummyAuthProvider } = ChromeUtils.importESModule(
            "moz-src:///toolkit/components/ipprotection/tests/IPPDummyAuthProvider.sys.mjs"
          );
          IPPDummyAuthProvider.setProxyPassError(error ?? null);
        },

        /* Toggle the test auth provider's sign-in state and recompute service state. */
        async simulateIPPSignIn(signedIn) {
          const { IPProtectionService } = ChromeUtils.importESModule(
            "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs"
          );
          const { IPPDummyAuthProvider } = ChromeUtils.importESModule(
            "moz-src:///toolkit/components/ipprotection/tests/IPPDummyAuthProvider.sys.mjs"
          );
          IPPDummyAuthProvider.simulateSignIn(signedIn);
          IPProtectionService.updateState();
        },

        /*
         * Returns the active proxy connection's proxyInfo (host, port, type),
         * or null when there is no active connection.
         */
        async getIPPProxyInfo() {
          const { IPPProxyManager } = ChromeUtils.importESModule(
            "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs"
          );
          const proxyInfo = IPPProxyManager.channelFilter()?.proxyInfo;
          if (!proxyInfo) {
            return null;
          }
          return {
            host: proxyInfo.host,
            port: proxyInfo.port,
            type: proxyInfo.type,
          };
        },
      },
    };
  }
};
