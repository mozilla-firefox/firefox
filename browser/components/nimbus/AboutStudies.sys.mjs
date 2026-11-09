/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTelemetry: "resource://nimbus/lib/Telemetry.sys.mjs",
  UnenrollmentCause: "resource://nimbus/lib/ExperimentManager.sys.mjs",
});

const SHIELD_LEARN_MORE_URL_PREF = "app.normandy.shieldLearnMoreUrl";

/**
 * The module exported by this file.
 */
export let AboutStudies = {};

/**
 * The weak set that keeps track of which browsing contexts
 * have an about:studies page.
 */
let BrowsingContexts = new WeakSet();
/**
 * about:studies page for displaying in-progress and past Shield studies.
 *
 * @type {AboutPage}
 * @implements {nsIMessageListener}
 */

ChromeUtils.defineLazyGetter(
  AboutStudies,
  "aboutStudies",
  () =>
    new (class {
      getMessagingSystemList() {
        const debugEnabled = Services.prefs.getBoolPref("nimbus.debug");

        // Do not include Firefox Labs. Those are shown on
        // about:preferences#experimental.
        //
        // Only show Rollouts if nimbus.debug is enabled.
        return lazy.ExperimentAPI.manager.store
          .getAll()
          .filter(e => !e.isFirefoxLabsOptIn && (debugEnabled || !e.isRollout));
      }

      async optInToExperiment(data) {
        try {
          await lazy.ExperimentAPI.optInToExperiment(data);
          return {
            error: false,
            message: "Opt-in was successful.",
          };
        } catch (error) {
          return {
            error: true,
            message: error.message,
          };
        }
      }

      /**
       * Add a browsing context to the weak set;
       * this weak set keeps track of all contexts
       * that are housing an about:studies page.
       */
      addToWeakSet(browsingContext) {
        BrowsingContexts.add(browsingContext);
      }
      /**
       * Remove a browsing context to the weak set;
       * this weak set keeps track of all contexts
       * that are housing an about:studies page.
       */
      removeFromWeakSet(browsingContext) {
        BrowsingContexts.delete(browsingContext);
      }

      /**
       * Sends a message to every about:studies page,
       * by iterating over the BrowsingContexts weakset.
       *
       * @param {string} message The message string to send to.
       * @param {object} data The data object to send.
       */
      _sendToAll(message, data) {
        ChromeUtils.nondeterministicGetWeakSetKeys(BrowsingContexts).forEach(
          browser =>
            browser.currentWindowGlobal
              .getActor("AboutStudies")
              .sendAsyncMessage(message, data)
        );
      }

      /**
       * Return whether or not studies are enabled.
       */
      async getStudiesEnabled() {
        await lazy.ExperimentAPI.ready();
        return lazy.ExperimentAPI.studiesEnabled;
      }

      async removeMessagingSystemExperiment(slug) {
        lazy.ExperimentAPI.manager.unenroll(
          slug,
          lazy.UnenrollmentCause.fromReason(
            lazy.NimbusTelemetry.UnenrollReason.INDIVIDUAL_OPT_OUT
          )
        );
        this._sendToAll(
          "Shield:UpdateMessagingSystemExperimentList",
          this.getMessagingSystemList()
        );
      }

      openDataPreferences() {
        const browserWindow =
          Services.wm.getMostRecentWindow("navigator:browser");
        browserWindow.openPreferences("privacy-reports");
      }
      getShieldLearnMoreHref() {
        return Services.urlFormatter.formatURLPref(SHIELD_LEARN_MORE_URL_PREF);
      }
    })()
);
