/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  let { ConsoleAPI } = ChromeUtils.importESModule(
    "resource://gre/modules/Console.sys.mjs"
  );
  let consoleOptions = {
    // tip: set maxLogLevel to "debug" and use log.debug() to create detailed
    // messages during development. See LOG_LEVELS in Console.sys.mjs for details.
    maxLogLevel: "error",
    maxLogLevelPref: "toolkit.backgroundtasks.loglevel",
    prefix: "BackgroundTasksUtils",
  };
  return new ConsoleAPI(consoleOptions);
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "ProfileService",
  "@mozilla.org/toolkit/profile-service;1",
  Ci.nsIToolkitProfileService
);

ChromeUtils.defineESModuleGetters(lazy, {
  ASRouter:
    // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
    "resource:///modules/asrouter/ASRouter.sys.mjs",
  ASRouterDefaultConfig:
    // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
    "resource:///modules/asrouter/ASRouterDefaultConfig.sys.mjs",

  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
});

class CannotLockProfileError extends Error {
  constructor(message) {
    super(message);
    this.name = "CannotLockProfileError";
  }
}

export var BackgroundTasksUtils = {
  // Manage our own default profile that can be overridden for testing.  It's
  // easier to do this here rather than using the profile service itself.
  _defaultProfileInitialized: false,
  _defaultProfile: null,

  getDefaultProfile() {
    if (!this._defaultProfileInitialized) {
      this._defaultProfileInitialized = true;
      // This is all test-only.
      let defaultProfilePath = Services.env.get(
        "MOZ_BACKGROUNDTASKS_DEFAULT_PROFILE_PATH"
      );
      let noDefaultProfile = Services.env.get(
        "MOZ_BACKGROUNDTASKS_NO_DEFAULT_PROFILE"
      );
      if (defaultProfilePath) {
        lazy.log.info(
          `getDefaultProfile: using default profile path ${defaultProfilePath}`
        );
        var tmpd = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
        tmpd.initWithPath(defaultProfilePath);
        // Sadly this writes to `profiles.ini`, but there's little to be done.
        this._defaultProfile = lazy.ProfileService.createProfile(
          tmpd,
          `MOZ_BACKGROUNDTASKS_DEFAULT_PROFILE_PATH-${Date.now()}`,
          "backgroundtasks"
        );
      } else if (noDefaultProfile) {
        lazy.log.info(`getDefaultProfile: setting default profile to null`);
        this._defaultProfile = null;
      } else {
        try {
          lazy.log.info(
            `getDefaultProfile: using ProfileService.defaultProfile`
          );
          this._defaultProfile = lazy.ProfileService.defaultProfile;
        } catch (e) {}
      }
    }
    return this._defaultProfile;
  },

  hasDefaultProfile() {
    return this.getDefaultProfile() != null;
  },

  currentProfileIsDefaultProfile() {
    let defaultProfile = this.getDefaultProfile();
    let currentProfile = lazy.ProfileService.currentProfile;
    // This comparison needs to accommodate null on both sides.
    let isDefaultProfile = defaultProfile && currentProfile == defaultProfile;
    return isDefaultProfile;
  },

  _throwIfNotLocked(lock) {
    if (!(lock instanceof Ci.nsIProfileLock)) {
      throw new Error("Passed lock was not an instance of nsIProfileLock");
    }

    try {
      // In release builds, `.directory` throws NS_ERROR_NOT_INITIALIZED when
      // unlocked.  In debug builds, `.directory` when the profile is not locked
      // will crash via `NS_ERROR`.
      if (lock.directory) {
        return;
      }
    } catch (e) {
      if (
        !(
          e instanceof Ci.nsIException &&
          e.result == Cr.NS_ERROR_NOT_INITIALIZED
        )
      ) {
        throw e;
      }
    }
    throw new Error("Profile is not locked");
  },

  /**
   * Locks the given profile and provides the path to it to the callback.
   * The callback should return a promise and once settled the profile is
   * unlocked and then the promise returned back to the caller of this function.
   *
   * @template T
   * @param {(lock: nsIProfileLock) => Promise<T>} callback
   * @param {nsIToolkitProfile} [profile] defaults to default profile
   * @returns {Promise<T>}
   */
  async withProfileLock(callback, profile = this.getDefaultProfile()) {
    if (!profile) {
      throw new Error("No default profile exists");
    }

    let lock;
    try {
      lock = profile.lock({});
      lazy.log.info(
        `withProfileLock: locked profile at ${lock.directory.path}`
      );
    } catch (e) {
      throw new CannotLockProfileError(`Cannot lock profile: ${e}`);
    }

    try {
      // We must await to ensure any logging is displayed after the callback resolves.
      return await callback(lock);
    } finally {
      try {
        lazy.log.info(
          `withProfileLock: unlocking profile at ${lock.directory.path}`
        );
        lock.unlock();
        lazy.log.info(`withProfileLock: unlocked profile`);
      } catch (e) {
        lazy.log.warn(`withProfileLock: error unlocking profile`, e);
      }
    }
  },

  /**
   * Reads the preferences from "prefs.js" out of a profile, optionally
   * returning only names satisfying a given predicate.
   *
   * If no `lock` is given, the default profile is locked and the preferences
   * read from it.  If `lock` is given, read from the given lock's directory.
   *
   * @param {(name: string) => boolean} [predicate] a predicate to filter
   * preferences by; if not given, all preferences are accepted.
   * @param {nsIProfileLock} [lock] optional lock to use
   * @returns {object} with keys that are string preference names and values
   * that are string|number|boolean preference values.
   */
  async readPreferences(predicate = null, lock = null) {
    if (!lock) {
      return this.withProfileLock(profileLock =>
        this.readPreferences(predicate, profileLock)
      );
    }

    this._throwIfNotLocked(lock);
    lazy.log.info(`readPreferences: profile is locked`);

    let prefs = {};
    let addPref = (kind, name, value) => {
      if (predicate && !predicate(name)) {
        return;
      }
      prefs[name] = value;
    };

    // We ignore any "user.js" file, since usage is low and doing otherwise
    // requires implementing a bit more of `nsIPrefsService` than feels safe.
    let prefsFile = lock.directory.clone();
    prefsFile.append("prefs.js");
    lazy.log.info(`readPreferences: will parse prefs ${prefsFile.path}`);

    let data = await IOUtils.read(prefsFile.path);
    lazy.log.debug(
      `readPreferences: parsing prefs from buffer of length ${data.length}`
    );

    Services.prefs.parsePrefsFromBuffer(
      data,
      {
        onStringPref: addPref,
        onIntPref: addPref,
        onBoolPref: addPref,
        onError(message) {
          // Firefox itself manages "prefs.js", so errors should be infrequent.
          lazy.log.error(message);
        },
      },
      prefsFile.path
    );

    lazy.log.debug(`readPreferences: parsed prefs from buffer`, prefs);
    return prefs;
  },

  /**
   * Reads the snapshotted Firefox Messaging System targeting out of a profile.
   *
   * If no `lock` is given, the default profile is locked and the preferences
   * read from it.  If `lock` is given, read from the given lock's directory.
   *
   * @param {nsIProfileLock} [lock] optional lock to use
   * @returns {object}
   */
  async readFirefoxMessagingSystemTargetingSnapshot(lock = null) {
    if (!lock) {
      return this.withProfileLock(profileLock =>
        this.readFirefoxMessagingSystemTargetingSnapshot(profileLock)
      );
    }

    this._throwIfNotLocked(lock);

    let snapshotFile = lock.directory.clone();
    snapshotFile.append("targeting.snapshot.json");

    lazy.log.info(
      `readFirefoxMessagingSystemTargetingSnapshot: will read Firefox Messaging ` +
        `System targeting snapshot from ${snapshotFile.path}`
    );

    return IOUtils.readJSON(snapshotFile.path);
  },

  /**
   * Reads the Telemetry Client ID out of a profile.
   *
   * If no `lock` is given, the default profile is locked and the preferences
   * read from it.  If `lock` is given, read from the given lock's directory.
   *
   * @param {nsIProfileLock} [lock] optional lock to use
   * @returns {string}
   */
  async readTelemetryClientID(lock = null) {
    if (!lock) {
      return this.withProfileLock(profileLock =>
        this.readTelemetryClientID(profileLock)
      );
    }

    this._throwIfNotLocked(lock);

    let stateFile = lock.directory.clone();
    stateFile.append("datareporting");
    stateFile.append("state.json");

    lazy.log.info(
      `readPreferences: will read Telemetry client ID from ${stateFile.path}`
    );

    let state = await IOUtils.readJSON(stateFile.path);

    return state.clientID;
  },

  /**
   * Enable the Nimbus experimentation framework.
   *
   * @param {nsICommandLine} commandLine if given, accept command line parameters
   *                                     like `--url about:studies?...` or
   *                                     `--url file:path/to.json` to explicitly
   *                                     opt-on to experiment branches.
   * @param {object} defaultProfile      snapshot of Firefox Messaging System
   *                                     targeting from default browsing profile.
   */
  async enableNimbus(commandLine, defaultProfile = {}) {
    await lazy.ExperimentAPI.init({
      forceSync: true,
      extraContext: { defaultProfile },
    });

    // Allow manual explicit opt-in to experiment branches to facilitate testing.
    //
    // Process command line arguments, like
    // `--url about:studies?optin_slug=nalexander-ms-test1&optin_branch=treatment-a&optin_collection=nimbus-preview`
    // or
    // `--url file:///Users/nalexander/Mozilla/gecko/experiment.json?optin_branch=treatment-a`.
    let ar;
    while ((ar = commandLine?.handleFlagWithParam("url", false))) {
      let uri = commandLine.resolveURI(ar);
      const params = new URLSearchParams(uri.query);

      if (uri.schemeIs("about") && uri.filePath == "studies") {
        // Allow explicit opt-in.  In the future, we might take this pref from
        // the default browsing profile.
        Services.prefs.setBoolPref("nimbus.debug", true);

        const data = {
          slug: params.get("optin_slug"),
          branch: params.get("optin_branch"),
          collection: params.get("optin_collection"),
        };
        await lazy.ExperimentAPI.optInToExperiment(data);
        lazy.log.info(`Opted in to experiment: ${JSON.stringify(data)}`);
      } else if (uri.schemeIs("file")) {
        let branchSlug = params.get("optin_branch");
        let path = decodeURIComponent(uri.filePath);
        let response = await fetch(uri.spec);
        let recipe = await response.json();
        if (recipe.permissions) {
          // Saved directly from Experimenter, there's a top-level `data`.  Hand
          // written, that's not the norm.
          recipe = recipe.data;
        }

        lazy.ExperimentAPI.manager.forceEnroll(recipe, branchSlug);
        lazy.log.info(`Forced enrollment into: ${path}, branch: ${branchSlug}`);
      }
    }
  },

  /**
   * Enable the Firefox Messaging System and, when successfully initialized,
   * trigger a message with trigger id `backgroundTask`.
   *
   * @param {object} defaultProfile - snapshot of Firefox Messaging System
   *                                  targeting from default browsing profile.
   */
  async enableFirefoxMessagingSystem(defaultProfile = {}) {
    function logArgs(tag, ...args) {
      lazy.log.debug(`FxMS invoked ${tag}: ${JSON.stringify(args)}`);
    }

    let { messageHandler, router, createStorage } =
      lazy.ASRouterDefaultConfig();

    if (!router.initialized) {
      const storage = await createStorage();
      await router.init({
        storage,
        // Background tasks never send legacy telemetry.
        sendTelemetry: logArgs.bind(null, "sendTelemetry"),
        dispatchCFRAction: messageHandler.handleCFRAction.bind(messageHandler),
        // There's no child process involved in background tasks, so swallow all
        // of these messages.
        updateAdminState: () => {},
      });
    }

    await lazy.ASRouter.waitForInitialized;

    const triggerId = "backgroundTask";
    await lazy.ASRouter.sendTriggerMessage({
      browser: null,
      id: triggerId,
      context: {
        defaultProfile,
      },
    });
    lazy.log.info(
      "Triggered Firefox Messaging System with trigger id 'backgroundTask'"
    );
  },
};
