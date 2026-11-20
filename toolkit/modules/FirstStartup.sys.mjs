/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  TaskScheduler: "resource://gre/modules/TaskScheduler.sys.mjs",
});

const PREF_TIMEOUT = "first-startup.timeout";
const PREF_CATEGORY_TASKS = "first-startup.category-tasks-enabled";
const CATEGORY_NAME = "first-startup-new-profile";

/**
 * Service for blocking application startup, to be used on the first install. The intended
 * use case is for `FirstStartup` to be invoked when the application is called by an installer,
 * such as the Windows Stub Installer, to allow the application to do some first-install tasks
 * such as performance tuning and downloading critical data.
 *
 * In this scenario, the installer does not exit until the first application window appears,
 * which gives the user experience of the application starting up quickly on first install.
 */
export var FirstStartup = {
  NOT_STARTED: 0,
  IN_PROGRESS: 1,
  TIMED_OUT: 2,
  SUCCESS: 3,
  UNSUPPORTED: 4,

  _state: 0, // NOT_STARTED,
  /**
   * Initialize and run first-startup services. This will always run synchronously
   * and spin the event loop until either all required services have
   * completed, or until a timeout is reached.
   *
   * In the latter case, services are expected to run post-UI instead as usual.
   *
   * @param {boolean} newProfile
   *   True if a new profile was just created, false otherwise.
   */
  init(newProfile) {
    if (!newProfile) {
      // In this case, we actually don't want to do any FirstStartup work,
      // since a pre-existing profile was detected (presumably, we entered here
      // because a user re-installed via the stub installer when there existed
      // previous user profiles on the file system). We do, however, want to
      // measure how often this occurs.
      Glean.firstStartup.statusCode.set(this.NOT_STARTED);
      Glean.firstStartup.newProfile.set(false);
      GleanPings.firstStartup.submit();
      return;
    }

    Glean.firstStartup.newProfile.set(true);

    this._state = this.IN_PROGRESS;
    const timeout = Services.prefs.getIntPref(PREF_TIMEOUT, 30000); // default to 30 seconds
    let startingTime = ChromeUtils.now();
    let initialized = false;

    let promises = [];

    let nimbusInitPromise = null;
    if (AppConstants.MOZ_BUILD_APP === "browser") {
      nimbusInitPromise = lazy.ExperimentAPI.init();
      promises.push(nimbusInitPromise);
    }

    let deleteTasksEndTime = null;
    if (AppConstants.MOZ_UPDATE_AGENT) {
      // It's technically possible for a previous installation to leave an old
      // OS-level scheduled task around.  Start fresh.
      promises.push(
        lazy.TaskScheduler.deleteAllTasks()
          .catch(() => {})
          .finally(() => {
            deleteTasksEndTime = ChromeUtils.now();
          })
      );
    }

    // Very important things that need to run before we launch the first window
    // during new profile setup can register a hook with CATEGORY_NAME.
    //
    // Consumers should be aware that:
    //
    // * This blocks first startup window opening for new installs on Windows
    //   ONLY for the first created default profile
    // * If PREF_TIMEOUT elapses before all of these promises complete, the
    //   registered category entries might not have all had a chance to
    //   successfully complete before the first browser window appears.
    const CATEGORY_TASKS_ENABLED = Services.prefs.getBoolPref(
      PREF_CATEGORY_TASKS,
      false
    );
    let categoryTasksEndTime = null;
    if (CATEGORY_TASKS_ENABLED && nimbusInitPromise) {
      promises.push(
        nimbusInitPromise.finally(() => {
          return lazy.BrowserUtils.callModulesFromCategory({
            categoryName: CATEGORY_NAME,
            profileMarker: "first-startup-new-profile-tasks",
            idleDispatch: false,
          }).finally(() => {
            categoryTasksEndTime = ChromeUtils.now();
          });
        })
      );
    }

    if (promises.length) {
      Promise.allSettled(promises).then(() => (initialized = true));

      this.elapsed = 0;
      Services.tm.spinEventLoopUntil("FirstStartup.sys.mjs:init", () => {
        this.elapsed = Math.ceil(ChromeUtils.now() - startingTime);
        if (this.elapsed >= timeout) {
          this._state = this.TIMED_OUT;
          return true;
        } else if (initialized) {
          this._state = this.SUCCESS;
          return true;
        }
        return false;
      });
    } else {
      this._state = this.UNSUPPORTED;
    }

    if (AppConstants.MOZ_BUILD_APP === "browser") {
      const nimbusTimestamps =
        lazy.ExperimentAPI.getAndClearFirstStartupTimestamps();

      Glean.firstStartup.nimbusStoreInitTime.set(
        Math.ceil(
          nimbusTimestamps?.storeInitEnd ?? ChromeUtils.now() - startingTime
        )
      );

      Glean.firstStartup.nimbusManagerInitTime.set(
        Math.ceil(
          nimbusTimestamps?.managerInitEnd ?? ChromeUtils.now() - startingTime
        )
      );

      Glean.firstStartup.nimbusLoaderInitTime.set(
        Math.ceil(
          nimbusTimestamps?.loaderInitEnd ?? ChromeUtils.now() - startingTime
        )
      );

      Glean.firstStartup.nimbusInitTime.set(
        Math.ceil(
          nimbusTimestamps?.nimbusInitEnd ?? ChromeUtils.now() - startingTime
        )
      );
    }

    if (AppConstants.MOZ_UPDATE_AGENT) {
      Glean.firstStartup.deleteTasksTime.set(
        Math.ceil(deleteTasksEndTime || ChromeUtils.now() - startingTime)
      );
    }

    if (CATEGORY_TASKS_ENABLED) {
      Glean.firstStartup.categoryTasksTime.set(
        Math.ceil(categoryTasksEndTime || ChromeUtils.now() - startingTime)
      );
    }

    Glean.firstStartup.statusCode.set(this._state);
    Glean.firstStartup.elapsed.set(this.elapsed);
    GleanPings.firstStartup.submit();
  },

  get state() {
    return this._state;
  },

  /**
   * For testing only. This puts us back into the initial NOT_STARTED state.
   */
  resetForTesting() {
    this._state = this.NOT_STARTED;
  },
};
