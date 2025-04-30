/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { clearTimeout, setTimeout } from "resource://gre/modules/Timer.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AppMenuNotifications: "resource://gre/modules/AppMenuNotifications.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "AppUpdateService",
  "@mozilla.org/updates/update-service;1",
  "nsIApplicationUpdateService"
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "UpdateManager",
  "@mozilla.org/updates/update-manager;1",
  "nsIUpdateManager"
);

const PREF_APP_UPDATE_UNSUPPORTED_URL = "app.update.unsupported.url";
const PREF_APP_UPDATE_SUPPRESS_PROMPTS = "app.update.suppressPrompts";

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "SUPPRESS_PROMPTS",
  PREF_APP_UPDATE_SUPPRESS_PROMPTS,
  false
);

// Setup the hamburger button badges for updates.
export var UpdateListener = {
  timeouts: [],

  restartDoorhangerShown: false,

  // Once a restart badge/doorhanger is scheduled, these store the time that
  // they were scheduled at (as milliseconds elapsed since the UNIX epoch). This
  // allows us to resume the badge/doorhanger timers rather than restarting
  // them from the beginning when a new update comes along.
  updateFirstReadyTime: null,

  // If PREF_APP_UPDATE_SUPPRESS_PROMPTS is true, we'll dispatch a notification
  // prompt 14 days from the last build time, or 7 days from the last update
  // time; whichever is sooner. It's hardcoded here to make sure update prompts
  // can't be suppressed permanently without knowledge of the consequences.
  promptDelayMsFromBuild: 14 * 24 * 60 * 60 * 1000, // 14 days

  promptDelayMsFromUpdate: 7 * 24 * 60 * 60 * 1000, // 7 days

  // If the last update time or current build time is more than 1 day in the
  // future, it has probably been manipulated and should be distrusted.
  promptMaxFutureVariation: 24 * 60 * 60 * 1000, // 1 day

  latestUpdate: null,

  availablePromptScheduled: false,

  get badgeWaitTime() {
    return Services.prefs.getIntPref("app.update.badgeWaitTime", 4 * 24 * 3600); // 4 days
  },

  async getSuppressedPromptDelay() {
    // Return the time (in milliseconds) after which a suppressed prompt should
    // be shown. Either 14 days from the last build time, or 7 days from the
    // last update time; whichever comes sooner. If build time is not available
    // and valid, schedule according to update time instead. If neither is
    // available and valid, schedule the prompt for right now. Times are checked
    // against the current time, since if the OS time is correct and nothing has
    // been manipulated, the build time and update time will always be in the
    // past. If the build time or update time is an hour in the future, it could
    // just be a timezone issue. But if it is more than 24 hours in the future,
    // it's probably due to attempted manipulation.
    let now = Date.now();
    let buildId = AppConstants.MOZ_BUILDID;
    let buildTime =
      new Date(
        buildId.slice(0, 4),
        buildId.slice(4, 6) - 1,
        buildId.slice(6, 8),
        buildId.slice(8, 10),
        buildId.slice(10, 12),
        buildId.slice(12, 14)
      ).getTime() ?? 0;
    const updateHistory = await lazy.UpdateManager.getHistory();
    let updateTime = updateHistory[0]?.installDate ?? 0;
    // Check that update/build times are at most 24 hours after now.
    if (buildTime - now > this.promptMaxFutureVariation) {
      buildTime = 0;
    }
    if (updateTime - now > this.promptMaxFutureVariation) {
      updateTime = 0;
    }
    let promptTime = now;
    // If both times are available, choose the sooner.
    if (updateTime && buildTime) {
      promptTime = Math.min(
        buildTime + this.promptDelayMsFromBuild,
        updateTime + this.promptDelayMsFromUpdate
      );
    } else if (updateTime || buildTime) {
      // When the update time is missing, this installation was probably just
      // installed and hasn't been updated yet. Ideally, we would instead set
      // promptTime to installTime + this.promptDelayMsFromUpdate. But it's
      // easier to get the build time than the install time. And on Nightly, the
      // times ought to be fairly close together anyways.
      promptTime = (updateTime || buildTime) + this.promptDelayMsFromUpdate;
    }
    return promptTime - now;
  },

  maybeShowUnsupportedNotification() {
    // Persist the unsupported notification across sessions. If at some point an
    // update is found this pref is cleared and the notification won't be shown.
    let url = Services.prefs.getCharPref(PREF_APP_UPDATE_UNSUPPORTED_URL, null);
    if (url) {
      this.showUpdateNotification(
        "unsupported",
        win => this.openUnsupportedUpdateUrl(win, url),
        true,
        { dismissed: true }
      );
    }
  },

  reset() {
    this.clearPendingAndActiveNotifications();
    this.restartDoorhangerShown = false;
    this.updateFirstReadyTime = null;
  },

  clearPendingAndActiveNotifications() {
    lazy.AppMenuNotifications.removeNotification(/^update-/);
    this.clearCallbacks();
  },

  clearCallbacks() {
    this.timeouts.forEach(t => clearTimeout(t));
    this.timeouts = [];
    this.availablePromptScheduled = false;
  },

  addTimeout(time, callback) {
    this.timeouts.push(
      setTimeout(() => {
        this.clearCallbacks();
        callback();
      }, time)
    );
  },

  requestRestart() {
    let cancelQuit = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
      Ci.nsISupportsPRBool
    );
    Services.obs.notifyObservers(
      cancelQuit,
      "quit-application-requested",
      "restart"
    );

    if (!cancelQuit.data) {
      Services.startup.quit(
        Services.startup.eAttemptQuit | Services.startup.eRestart
      );
    }
  },

  openManualUpdateUrl(win) {
    let manualUpdateUrl = Services.urlFormatter.formatURLPref(
      "app.update.url.manual"
    );
    win.openURL(manualUpdateUrl);
  },

  openUnsupportedUpdateUrl(win, detailsURL) {
    win.openURL(detailsURL);
  },

  getReleaseNotesUrl(update) {
    try {
      // Release notes are enabled by default for EN locales only, but can be
      // enabled for other locales within an experiment.
      if (
        !Services.locale.appLocaleAsBCP47.startsWith("en") &&
        !lazy.NimbusFeatures.updatePrompt.getVariable("showReleaseNotesLink")
      ) {
        return null;
      }
      // The release notes URL is set in the pref app.releaseNotesURL.prompt,
      // but can also be overridden by an experiment.
      let url = lazy.NimbusFeatures.updatePrompt.getVariable("releaseNotesURL");
      if (url) {
        let versionString = update.appVersion;
        switch (update.channel) {
          case "aurora":
          case "beta":
            versionString += "beta";
            break;
        }
        url = Services.urlFormatter.formatURL(
          url.replace("%VERSION%", versionString)
        );
      }
      return url || null;
    } catch (error) {
      return null;
    }
  },

  showUpdateNotification(type, mainAction, mainActionDismiss, options = {}) {
    const addTelemetry = metric => {
      // No telemetry for the "downloading" state.
      if (type !== "downloading") {
        // Histogram category labels can't have dashes in them.
        let telemetryType = type.replaceAll("-", "");
        metric[telemetryType].add();
      }
    };
    let action = {
      callback(win, fromDoorhanger) {
        if (fromDoorhanger) {
          addTelemetry(Glean.update.notificationMainActionDoorhanger);
        } else {
          addTelemetry(Glean.update.notificationMainActionMenu);
        }
        mainAction(win);
      },
      dismiss: mainActionDismiss,
    };

    let secondaryAction = {
      callback() {
        addTelemetry(Glean.update.notificationDismissed);
      },
      dismiss: true,
    };
    lazy.AppMenuNotifications.showNotification(
      "update-" + type,
      action,
      secondaryAction,
      options
    );
    if (options.dismissed) {
      addTelemetry(Glean.update.notificationBadgeShown);
    } else {
      addTelemetry(Glean.update.notificationShown);
    }
  },

  showRestartNotification(update, dismissed) {
    let notification = lazy.AppUpdateService.isOtherInstanceHandlingUpdates
      ? "other-instance"
      : "restart";
    if (!dismissed) {
      this.restartDoorhangerShown = true;
    }
    this.showUpdateNotification(
      notification,
      () => this.requestRestart(),
      true,
      { dismissed }
    );
  },

  showUpdateAvailableNotification(update, dismissed) {
    let learnMoreURL = this.getReleaseNotesUrl(update);
    this.showUpdateNotification(
      "available",
      // This is asynchronous, but we are just going to kick it off.
      () => lazy.AppUpdateService.downloadUpdate(update, true),
      false,
      { dismissed, learnMoreURL }
    );
    lazy.NimbusFeatures.updatePrompt.recordExposureEvent({ once: true });
  },

  showManualUpdateNotification(update, dismissed) {
    let learnMoreURL = this.getReleaseNotesUrl(update);
    this.showUpdateNotification(
      "manual",
      win => this.openManualUpdateUrl(win),
      false,
      { dismissed, learnMoreURL }
    );
    lazy.NimbusFeatures.updatePrompt.recordExposureEvent({ once: true });
  },

  showUnsupportedUpdateNotification(update, dismissed) {
    if (!update || !update.detailsURL) {
      console.error(
        "The update for an unsupported notification must have a " +
          "detailsURL attribute."
      );
      return;
    }
    let url = update.detailsURL;
    if (
      url != Services.prefs.getCharPref(PREF_APP_UPDATE_UNSUPPORTED_URL, null)
    ) {
      Services.prefs.setCharPref(PREF_APP_UPDATE_UNSUPPORTED_URL, url);
      this.showUpdateNotification(
        "unsupported",
        win => this.openUnsupportedUpdateUrl(win, url),
        true,
        { dismissed }
      );
    }
  },

  showUpdateDownloadingNotification() {
    this.showUpdateNotification(
      "downloading",
      // The user clicked on the "Downloading update" app menu item.
      // Code in browser/components/customizableui/content/panelUI.js
      // receives the following notification and opens the about dialog.
      () => Services.obs.notifyObservers(null, "show-update-progress"),
      true,
      { dismissed: true }
    );
  },

  async scheduleUpdateAvailableNotification(update) {
    // Show a badge/banner-only notification immediately.
    this.showUpdateAvailableNotification(update, true);
    // Track the latest update, since we will almost certainly have a new update
    // 7 days from now. In a common scenario, update 1 triggers the timer.
    // Updates 2, 3, 4, and 5 come without opening a prompt, since one is
    // already scheduled. Then, the timer ends and the prompt that was triggered
    // by update 1 is opened. But rather than downloading update 1, of course,
    // it will download update 5, the latest update.
    this.latestUpdate = update;
    // Only schedule one doorhanger at a time. If we don't, then a new
    // doorhanger would be scheduled at least once per day. If the user
    // downloads the first update, we don't want to keep alerting them.
    if (!this.availablePromptScheduled) {
      const suppressedPromptDelay = await this.getSuppressedPromptDelay();
      this.addTimeout(Math.max(0, suppressedPromptDelay), () => {
        // If we downloaded or installed an update via the badge or banner
        // while the timer was running, bail out of showing the doorhanger.
        if (
          lazy.AppUpdateService.currentState !=
          Ci.nsIApplicationUpdateService.STATE_IDLE
        ) {
          return;
        }
        this.showUpdateAvailableNotification(this.latestUpdate, false);
      });
      this.availablePromptScheduled = true;
    }
  },

  handleUpdateError(update, status) {
    switch (status) {
      case "download-attempt-failed":
        this.clearCallbacks();
        this.showUpdateAvailableNotification(update, false);
        break;
      case "download-attempts-exceeded":
        this.clearCallbacks();
        this.showManualUpdateNotification(update, false);
        break;
      case "elevation-attempt-failed":
        this.clearCallbacks();
        this.showRestartNotification(false);
        break;
      case "elevation-attempts-exceeded":
        this.clearCallbacks();
        this.showManualUpdateNotification(update, false);
        break;
      case "check-attempts-exceeded":
      case "unknown":
      case "bad-perms":
        // Background update has failed, let's show the UI responsible for
        // prompting the user to update manually.
        this.clearCallbacks();
        this.showManualUpdateNotification(update, false);
        break;
    }
  },

  handleUpdateStagedOrDownloaded(update, status) {
    switch (status) {
      case "applied":
      case "pending":
      case "applied-service":
      case "pending-service":
      case "pending-elevate":
      case "success": {
        this.clearCallbacks();

        let initialBadgeWaitTimeMs = this.badgeWaitTime * 1000;
        let initialDoorhangerWaitTimeMs = update.promptWaitTime * 1000;
        let now = Date.now();

        if (!this.updateFirstReadyTime) {
          this.updateFirstReadyTime = now;
        }

        let badgeWaitTimeMs = Math.max(
          0,
          this.updateFirstReadyTime + initialBadgeWaitTimeMs - now
        );
        let doorhangerWaitTimeMs = Math.max(
          0,
          this.updateFirstReadyTime + initialDoorhangerWaitTimeMs - now
        );

        // On Nightly only, permit disabling doorhangers for update restart
        // notifications by setting PREF_APP_UPDATE_SUPPRESS_PROMPTS
        if (AppConstants.NIGHTLY_BUILD && lazy.SUPPRESS_PROMPTS) {
          this.showRestartNotification(update, true);
        } else if (badgeWaitTimeMs < doorhangerWaitTimeMs) {
          this.addTimeout(badgeWaitTimeMs, () => {
            this.showRestartNotification(update, true);

            if (!this.restartDoorhangerShown) {
              // doorhangerWaitTimeMs is relative to when we initially received
              // the event. Since we've already waited badgeWaitTimeMs, subtract
              // that from doorhangerWaitTimeMs.
              let remainingTime = doorhangerWaitTimeMs - badgeWaitTimeMs;
              this.addTimeout(remainingTime, () => {
                this.showRestartNotification(update, false);
              });
            }
          });
        } else {
          this.addTimeout(doorhangerWaitTimeMs, () => {
            this.showRestartNotification(update, this.restartDoorhangerShown);
          });
        }
        break;
      }
    }
  },

  async handleUpdateAvailable(update, status) {
    switch (status) {
      case "show-prompt":
        // If an update is available, show an update available doorhanger unless
        // PREF_APP_UPDATE_SUPPRESS_PROMPTS is true (only on Nightly).
        if (AppConstants.NIGHTLY_BUILD && lazy.SUPPRESS_PROMPTS) {
          await this.scheduleUpdateAvailableNotification(update);
        } else {
          this.showUpdateAvailableNotification(update, false);
        }
        break;
      case "cant-apply":
        this.clearCallbacks();
        this.showManualUpdateNotification(update, false);
        break;
      case "unsupported":
        this.clearCallbacks();
        this.showUnsupportedUpdateNotification(update, false);
        break;
    }
  },

  handleUpdateDownloading(status) {
    switch (status) {
      case "downloading":
        this.showUpdateDownloadingNotification();
        break;
      case "idle":
        this.clearPendingAndActiveNotifications();
        break;
    }
  },

  handleUpdateSwap() {
    // This function is called because we just finished downloading an update
    // (possibly) when another update was already ready.
    // At some point, we may want to have some sort of intermediate
    // notification to display here so that the badge doesn't just disappear.
    // Currently, this function just hides update notifications and clears
    // the callback timers so that notifications will not be shown. We want to
    // clear the restart notification so the user doesn't try to restart to
    // update during staging. We want to clear any other notifications too,
    // since none of them make sense to display now.
    // Our observer will fire again when the update is either ready to install
    // or an error has been encountered.
    this.clearPendingAndActiveNotifications();
  },

  async observe(subject, topic, status) {
    let update = subject && subject.QueryInterface(Ci.nsIUpdate);

    switch (topic) {
      case "update-available":
        if (status != "unsupported") {
          // An update check has found an update so clear the unsupported pref
          // in case it is set.
          Services.prefs.clearUserPref(PREF_APP_UPDATE_UNSUPPORTED_URL);
        }
        await this.handleUpdateAvailable(update, status);
        break;
      case "update-downloading":
        this.handleUpdateDownloading(status);
        break;
      case "update-staged":
      case "update-downloaded":
        // An update check has found an update and downloaded / staged the
        // update so clear the unsupported pref in case it is set.
        Services.prefs.clearUserPref(PREF_APP_UPDATE_UNSUPPORTED_URL);
        this.handleUpdateStagedOrDownloaded(update, status);
        break;
      case "update-error":
        this.handleUpdateError(update, status);
        break;
      case "update-swap":
        this.handleUpdateSwap();
        break;
    }
  },
};
