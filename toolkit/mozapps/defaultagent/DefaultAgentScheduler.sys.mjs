/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  TaskScheduler: "resource://gre/modules/TaskScheduler.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () =>
  console.createInstance({
    maxLogLevel: "Error",
    maxLogLevelPref: "app.defaultagent.loglevel",
    logModule: "DefaultAgentScheduler",
  })
);

const TASK_MODULE = "defaultagent";

// The WinRT task registration name (and identity). The MSIX background task COM
// server splits on ":" - firefox --backgroundtask TASK_MODULE do-task`.
const TASK_ID = `${TASK_MODULE}:do-task`;
const INTERVAL_SECONDS = 24 * 60 * 60;
const ENABLED_PREF = "default-browser-agent.enabled";

// Records the task parameters (name, interval, and entry point) the task was
// last registered with.
const REGISTERED_PARAMS_PREF = "default-browser-agent.msix.registered-params";

// Recorded as JSON because the task name itself contains ":"
function taskParams() {
  return JSON.stringify({
    id: TASK_ID,
    intervalSeconds: INTERVAL_SECONDS,
    entryPoint: AppConstants.MOZ_BACKGROUNDTASK_ACTIVATABLE_CLASS_ID,
  });
}

/**
 * The task name recorded by a previous registration, if it differs from the
 * name in use now.
 *
 * @returns {?string} the stale task name, or null if there isn't one.
 */
function staleTaskId() {
  let recorded = Services.prefs.getStringPref(REGISTERED_PARAMS_PREF, "");
  if (!recorded) {
    return null;
  }

  let id;
  try {
    id = JSON.parse(recorded).id;
  } catch (e) {
    // Written by a version that recorded a different format; nothing to remove.
    return null;
  }

  return id && id != TASK_ID ? id : null;
}

async function deleteTaskIfRegistered(id) {
  if (!id) {
    return;
  }

  try {
    await lazy.TaskScheduler.deleteTask(id);
    lazy.log.info(`Removed previously registered task "${id}".`);
  } catch (e) {
    // Throws NS_ERROR_FILE_NOT_FOUND when there is no such task.
    lazy.log.error(`Could not remove previously registered task: ${e}`);
  }
}

/**
 * Schedules the Windows default browser agent on MSIX (packaged) installs.
 *
 * This is the packaged-install counterpart to the classic registration done by
 * the NSIS installer via default-browser-agent.exe.
 */
export const DefaultAgentScheduler = {
  TASK_ID,
  INTERVAL_SECONDS,

  _initialized: false,

  // The MSIX path only applies to packaged Windows builds that actually ship
  // the default agent component.
  get appliesToThisInstall() {
    return (
      AppConstants.platform == "win" &&
      "@mozilla.org/default-agent;1" in Cc &&
      Services.sysinfo.getProperty("hasWinPackageId")
    );
  },

  get disabled() {
    let agent = Cc["@mozilla.org/default-agent;1"].getService(
      Ci.nsIDefaultAgent
    );
    return agent.agentDisabled();
  },

  observe(subject, topic, data) {
    if (topic == "nsPref:changed" && data == ENABLED_PREF) {
      this.maybeScheduleDefaultAgentTask().catch(e => lazy.log.error(e));
    }
  },

  /**
   * Register or unregister the background task to match the current enabled
   * state. Safe to call on every startup: it only registers when there is no
   * task yet or when an update changed the task parameters, so ordinary
   * restarts leave an existing registration untouched.
   *
   * @returns {Promise<boolean>} true if a task is scheduled afterwards.
   */
  async maybeScheduleDefaultAgentTask() {
    if (!this.appliesToThisInstall) {
      return false;
    }

    if (!this._initialized) {
      Services.prefs.addObserver(ENABLED_PREF, this);
      this._initialized = true;
    }

    let exists = false;
    try {
      exists = await lazy.TaskScheduler.taskExists(TASK_ID);
    } catch (e) {
      // State is unknown; fall through and (re-)register below.
      lazy.log.error(`taskExists failed: ${e}`);
    }

    let stale = staleTaskId();

    if (this.disabled) {
      if (exists) {
        await lazy.TaskScheduler.deleteTask(TASK_ID);
      }
      await deleteTaskIfRegistered(stale);
      if (exists || stale) {
        Services.prefs.clearUserPref(REGISTERED_PARAMS_PREF);
        lazy.log.info("Agent disabled; deleted default agent task.");
      }
      return false;
    }

    // Only (re-)register when necessary, on first run, or after an update that
    // changed the task parameters. Note: in the future, if we notice this changing
    // quite frequently, we should switch to using the registry to preserve the daily cadence
    // of the defaultagent.
    const params = taskParams();
    if (
      exists &&
      Services.prefs.getStringPref(REGISTERED_PARAMS_PREF, "") == params
    ) {
      return true;
    }

    // A previous version may have registered under a different name. Windows
    // keeps that registration until it is explicitly unregistered, so it would
    // otherwise keep launching the task alongside the new registration.
    await deleteTaskIfRegistered(stale);

    // The WinRT BackgroundTask API does not use the command arg.
    await lazy.TaskScheduler.registerTask(TASK_ID, null, INTERVAL_SECONDS);
    Services.prefs.setStringPref(REGISTERED_PARAMS_PREF, params);
    lazy.log.info("Agent enabled; registered default agent task.");
    return true;
  },
};
