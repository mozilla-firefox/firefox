/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "timerManager",
  "@mozilla.org/updates/timer-manager;1",
  Ci.nsIUpdateTimerManager
);

ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  HeartbeatSurvey:
    "moz-src:///browser/components/nimbus/HeartbeatSurvey.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  const { Logger } = ChromeUtils.importESModule(
    "resource://messaging-system/lib/Logger.sys.mjs"
  );
  return new Logger("LegacyHeartbeatFeature");
});

const TIMER_NAME = "legacy-heartbeat";
const PREF_CHANGED_TOPIC = "nsPref:changed";
const RUN_INTERVAL_PREF = "app.normandy.run_interval_seconds";
const SHIELD_ENABLED_PREF = "app.normandy.enabled";
const API_URL_PREF = "app.normandy.api_url";

const TIMER_LAST_UPDATE_PREF = `app.update.lastUpdateTime.${TIMER_NAME}`;

/**
 * Runs legacy Heartbeat surveys through Nimbus.
 */
export const LegacyHeartbeatFeature = new (class {
  #initialized;
  #onFeatureUpdate;
  #running;
  #shutdownRegistered;

  init() {
    if (this.#initialized) {
      return;
    }

    const backgroundTasks = Cc["@mozilla.org/backgroundtasks;1"]?.getService(
      Ci.nsIBackgroundTasks
    );
    const inShutdown = Services.startup.isInOrBeyondShutdownPhase(
      Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
    );
    if (backgroundTasks?.isBackgroundTaskMode || inShutdown) {
      return;
    }

    this.#onFeatureUpdate = () => {
      this.run({ trigger: "featureUpdate" });
    };
    lazy.NimbusFeatures.legacyHeartbeat.onUpdate(this.#onFeatureUpdate);

    this.setTimer();
    Services.prefs.addObserver(RUN_INTERVAL_PREF, this);
    this.#initialized = true;

    if (!this.#shutdownRegistered) {
      lazy.AsyncShutdown.appShutdownConfirmed.addBlocker(
        "LegacyHeartbeatFeature: cleaning up",
        () => this.uninit()
      );
      this.#shutdownRegistered = true;
    }
  }

  async uninit() {
    if (!this.#initialized) {
      return;
    }

    lazy.NimbusFeatures.legacyHeartbeat.offUpdate(this.#onFeatureUpdate);
    lazy.timerManager.unregisterTimer(TIMER_NAME);

    this.#initialized = false;
    this.#onFeatureUpdate = null;
    Services.prefs.removeObserver(RUN_INTERVAL_PREF, this);
  }

  observe(subject, topic, data) {
    if (topic === PREF_CHANGED_TOPIC && data === RUN_INTERVAL_PREF) {
      this.setTimer();
    }
  }

  checkPrefs() {
    if (!Services.prefs.getBoolPref(SHIELD_ENABLED_PREF, false)) {
      return false;
    }

    const apiUrl = Services.prefs.getCharPref(API_URL_PREF, "");

    if (!apiUrl) {
      return false;
    }

    if (!apiUrl.startsWith("https://")) {
      return false;
    }

    return true;
  }

  setTimer() {
    const runInterval = Services.prefs.getIntPref(RUN_INTERVAL_PREF, 21600);
    lazy.timerManager.registerTimer(TIMER_NAME, () => this.run(), runInterval);
  }

  async run({ trigger = "timer" } = {}) {
    if (this.#running || !this.checkPrefs()) {
      return;
    }

    this.#running = true;

    try {
      const recipe = this.getHeartbeatRecipe();

      if (recipe === null) {
        return;
      }

      const survey = new lazy.HeartbeatSurvey();
      await survey.run(recipe);
    } catch (e) {
      lazy.log.error(e);
    } finally {
      this.#running = false;

      if (trigger != "timer") {
        // `run()` was executed outside the scheduled timer.
        // Update the last time it ran to make sure it is rescheduled later.
        const lastUpdateTime = Math.round(Date.now() / 1000);
        Services.prefs.setIntPref(TIMER_LAST_UPDATE_PREF, lastUpdateTime);
      }
    }
  }

  getHeartbeatRecipe() {
    const survey = lazy.NimbusFeatures.legacyHeartbeat.getVariable("survey");

    if (typeof survey == "undefined") {
      return null;
    }

    const { slug } =
      lazy.NimbusFeatures.legacyHeartbeat.getEnrollmentMetadata();
    return {
      id: `nimbus:${slug}`,
      arguments: survey,
    };
  }
})();
