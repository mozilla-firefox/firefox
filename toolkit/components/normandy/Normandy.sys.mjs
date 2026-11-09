/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Log } from "resource://gre/modules/Log.sys.mjs";
import { setTimeout } from "resource://gre/modules/Timer.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  CleanupManager: "resource://normandy/lib/CleanupManager.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  LogManager: "resource://normandy/lib/LogManager.sys.mjs",
});

const UI_AVAILABLE_NOTIFICATION = "sessionstore-windows-restored";
const SHIELD_INIT_NOTIFICATION = "shield-init-complete";

const PREF_LOGGING_LEVEL = "app.normandy.logging.level";

export var Normandy = {
  uiAvailableNotificationObserved: Promise.withResolvers(),

  /** Initialization that needs to happen before the first paint on startup. */
  async init({ runAsync = true } = {}) {
    // NOTE: It looks like we can see us being called twice between init(true)
    // coming from "browser-before-ui-startup" (see BrowserComponents.manifest)
    // and init(false) coming from FirstStartup.sys.mjs.
    // We need the UI_AVAILABLE_NOTIFICATION observer only if runAsync == true
    // and we assume that the rest of the initialization can just happen twice.
    // TODO: Check which pieces really need to run twice, if any.
    if (runAsync) {
      // It is important to register the listener for the UI before the first
      // await, to avoid missing it.
      Services.obs.addObserver(this, UI_AVAILABLE_NOTIFICATION);
    }

    // Wait for the UI to be ready, or time out after 5 minutes.
    if (runAsync) {
      await Promise.race([
        this.uiAvailableNotificationObserved.promise,
        new Promise(resolve => setTimeout(resolve, 5 * 60 * 1000)),
      ]);

      // Remove observer for UI notifications.
      Services.obs.removeObserver(this, UI_AVAILABLE_NOTIFICATION);
    }

    await this.finishInit();
  },

  async observe(subject, topic) {
    if (topic === UI_AVAILABLE_NOTIFICATION) {
      this.uiAvailableNotificationObserved.resolve();
    }
  },

  async finishInit() {
    // Setup logging and listen for changes to logging prefs
    lazy.LogManager.configure(
      Services.prefs.getIntPref(PREF_LOGGING_LEVEL, Log.Level.Warn)
    );
    Services.prefs.addObserver(PREF_LOGGING_LEVEL, lazy.LogManager.configure);
    lazy.CleanupManager.addCleanupHandler(() =>
      Services.prefs.removeObserver(
        PREF_LOGGING_LEVEL,
        lazy.LogManager.configure
      )
    );

    await lazy.ExperimentAPI.init();

    Services.obs.notifyObservers(null, SHIELD_INIT_NOTIFICATION);
  },

  async uninit() {
    await lazy.CleanupManager.cleanup();
    // Note that Service.pref.removeObserver and Service.obs.removeObserver have
    // oppositely ordered parameters.
    Services.prefs.removeObserver(
      PREF_LOGGING_LEVEL,
      lazy.LogManager.configure
    );

    try {
      Services.obs.removeObserver(this, UI_AVAILABLE_NOTIFICATION);
    } catch (e) {
      // topic must have already been removed or never added
    }
  },
};
