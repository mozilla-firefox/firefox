/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
const PREF_APP_UPDATE_COMPULSORY_RESTART = "app.update.compulsory_restart";
let deferredRestartTasks = null;

const postWakeRestartDeferralMilliseconds = 5 * 60 * 1000; // 5 minutes

ChromeUtils.defineESModuleGetters(lazy, {
  ScheduledTask: "resource://gre/modules/ScheduledTask.sys.mjs",
  InfoBar: "resource:///modules/asrouter/InfoBar.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () =>
  console.createInstance({
    logModule: "UpdatePolicyEnforcer",
    maxLogLevelPref: "app.update.compulsory_restart_log",
  })
);

// Forcibly close Firefox, without waiting for beforeunload handlers.
function forceRestart() {
  lazy.logConsole.warn(`Firefox is restarting`);
  Services.startup.quit(
    Services.startup.eForceQuit | Services.startup.eRestart
  );
}

function infobarDispatchCallback(action, _selectedBrowser) {
  if (action?.type === "USER_ACTION" && action.data?.type === "RESTART_APP") {
    forceRestart();
  } else {
    lazy.logConsole.debug(
      `No action for type=${action?.type} and data.type=${action.data?.type}`
    );
  }
}

// Display the notification message
function showNotificationToolbar(restartZonedDateTime) {
  const datetime = restartZonedDateTime.epochMilliseconds;

  const message = {
    weight: 100,
    id: "COMPULSORY_RESTART_SCHEDULED",
    content: {
      priority: 3,
      type: "universal",
      dismissable: false,
      text: {
        string_id: "compulsory-restart-message",
      },
      buttons: [
        {
          label: {
            string_id: "policy-update-now",
          },
          action: {
            type: "RESTART_APP",
            dismiss: false,
          },
        },
      ],
      attributes: {
        datetime,
      },
    },
    template: "infobar",
    targeting: "true",
    groups: [],
  };

  const win = Services.wm.getMostRecentBrowserWindow("navigator:browser");
  if (!win) {
    return;
  }
  lazy.InfoBar.showInfoBarMessage(
    win.gBrowser.selectedBrowser,
    message,
    infobarDispatchCallback
  );
  lazy.logConsole.info(`Showing infobar message`);
}

/**
 * Disarm deferred tasks and clear the state for the next test.
 */
export function testingOnly_resetTasks() {
  if (!Cu.isInAutomation) {
    throw new Error("this method only usable in testing");
  }
  deferredRestartTasks?.notificationTask?.disarm();
  deferredRestartTasks?.restartTask?.disarm();
  deferredRestartTasks = null;
}

/**
 * Check on whether the deferred tasks have been created and armed.
 */
export function testingOnly_getTaskStatus() {
  if (!Cu.isInAutomation) {
    throw new Error("this method only usable in testing");
  }
  if (deferredRestartTasks) {
    const res = {
      notificationTask: deferredRestartTasks.notificationTask?.isArmed,
      restartTask: deferredRestartTasks.restartTask?.isArmed,
    };
    return res;
  }
  return null;
}

// Example settings:
// {NotificationPeriodHours: 72, RestartTimeOfDay: {Hour: 18, Minute: 45}}

/**
 * Params:
 * nowInstant: A Temporal.Instant object representing the current time.
 * notificationPeriodHours: The minimum amount of time in hours between when an update has been staged and when a warning will be displayed
 * restartTimeOfDay: The time of day, in local time, that the restart will take place, in the form of an object like: {hour: 3, minute: 14}
 *
 * Returns:
 * {
 *  notificationZonedDateTime: a Temporal.ZonedDateTime object representing the time when the notification bar should start to be shown.
 *  restartZonedDateTime: a Temporal.ZonedDateTime object representing the time when the current Firefox instance will forcibly quit
 * }
 */
export function calculateSchedule(
  nowInstant,
  notificationPeriodHours,
  restartTimeOfDay
) {
  // The notification time is nowInstant + notificationPeriodHours
  const notificationDelay = Temporal.Duration.from({
    hours: notificationPeriodHours,
  });
  const notificationInstant = nowInstant.add(notificationDelay);
  const notificationZonedDateTime = notificationInstant.toZonedDateTimeISO(
    Temporal.Now.timeZoneId()
  );

  // Figure out the compulsory restart time. The earliest is 1 hour past the notification time, the latest is 25 hours past the notification time.
  // restart time = if (there is an upcoming `restartTimeOfDay` on the notification day, an it's more than an hour in the future) then (notification day at restart time) else (notification day + 1 at restart time)
  const restartTime = Temporal.PlainTime.from({
    hour: restartTimeOfDay.Hour,
    minute: restartTimeOfDay.Minute,
  });

  let restartZonedDateTime =
    notificationZonedDateTime.withPlainTime(restartTime);

  // Make sure the user has at least 1 hour notification before restart time.
  // If not, postpone the restart by 24 hours.
  if (
    Temporal.Duration.compare(
      notificationZonedDateTime.until(restartZonedDateTime),
      Temporal.Duration.from({ hours: 1 })
    ) < 0
  ) {
    // it's less than 1 hour until the restart time.
    restartZonedDateTime = restartZonedDateTime.add(
      Temporal.Duration.from({ hours: 24 })
    );
  }
  lazy.logConsole.debug(
    `Computed notification time: ${notificationZonedDateTime}, restart time: ${restartZonedDateTime}`
  );
  return { notificationZonedDateTime, restartZonedDateTime };
}

// Create scheduled tasks with requested date/time
export function createScheduledRestartTasks(
  restartZonedDateTime,
  notificationZonedDateTime
) {
  const notificationTask = new lazy.ScheduledTask(() => {
    lazy.logConsole.debug(
      `notification task triggered, will show notification bar`
    );
    showNotificationToolbar(restartZonedDateTime);
  }, notificationZonedDateTime.epochMilliseconds);
  const restartTask = new lazy.ScheduledTask(
    forceRestart,
    restartZonedDateTime.epochMilliseconds,
    postWakeRestartDeferralMilliseconds
  );
  notificationTask.arm();
  restartTask.arm();
  lazy.logConsole.info(
    `Restart scheduled for ${restartZonedDateTime}, notification begins at ${notificationZonedDateTime}`
  );
  return { notificationTask, restartTask };
}

// Read the policy from prefs and parse the JSON.
export function getCompulsoryRestartPolicy() {
  const compulsoryRestartSettingStr = Services.prefs.getStringPref(
    PREF_APP_UPDATE_COMPULSORY_RESTART,
    null
  );
  if (compulsoryRestartSettingStr) {
    const compulsoryRestartSetting = JSON.parse(compulsoryRestartSettingStr);
    if (
      typeof compulsoryRestartSetting?.NotificationPeriodHours === "number" &&
      typeof compulsoryRestartSetting?.RestartTimeOfDay === "object" &&
      typeof compulsoryRestartSetting.RestartTimeOfDay.Hour === "number" &&
      typeof compulsoryRestartSetting.RestartTimeOfDay.Minute === "number"
    ) {
      lazy.logConsole.debug(
        `Compulsory restart policy: ${compulsoryRestartSetting}`
      );
      return compulsoryRestartSetting;
    }
  }
  lazy.logConsole.debug(`No compulsory restart policy`);
  return null;
}

// This is the main entry point into this module.
// This function is called when an update is staged, to set timers for
// when to show the notification and when to force a restart.
export function handleCompulsoryUpdatePolicy() {
  if (!deferredRestartTasks) {
    const compulsoryRestartSetting = getCompulsoryRestartPolicy();
    if (compulsoryRestartSetting) {
      const now = Temporal.Now.instant();
      const { restartZonedDateTime, notificationZonedDateTime } =
        calculateSchedule(
          now,
          compulsoryRestartSetting.NotificationPeriodHours,
          compulsoryRestartSetting.RestartTimeOfDay
        );
      if (restartZonedDateTime && notificationZonedDateTime) {
        deferredRestartTasks = createScheduledRestartTasks(
          restartZonedDateTime,
          notificationZonedDateTime
        );
      } else {
        lazy.logConsole.error(
          `Invalid restart settings: ${JSON.stringify(compulsoryRestartSetting)}`
        );
      }
    }
  }
}

const observer = {
  observe: (_subject, topic, _data) => {
    switch (topic) {
      case "update-downloaded":
      case "update-staged":
        handleCompulsoryUpdatePolicy();
        break;
    }
  },
};

export const UpdatePolicyEnforcer = {
  registerObservers() {
    Services.obs.addObserver(observer, "update-downloaded");
    Services.obs.addObserver(observer, "update-staged");
  },
};
