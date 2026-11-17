/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Invoke this task like `firefox.exe --backgroundtask message ...`.
//
// This task is complicated because it's meant for manual testing by QA but also
// for automated testing.  We might split these two functions at some point.
//
// First, note that the task takes significant configuration from the command
// line.  This is different than the eventual home for this functionality, the
// background update task, which will take this configuration from the default
// browsing profile.
//
// This task accepts the following command line arguments:
//
// --debug: enable very verbose debug logging.  Note that the `MOZ_LOG`
// environment variables still apply.
//
// --stage: use stage Remote Settings
//   (`https://firefox.settings.services.allizom.org/v1`) rather than production
//   (`https://firefox.settings.services.mozilla.com/v1`)
//
// --preview: enable Remote Settings and Experiment previews.
//
// `--url about:studies?...` (as copy-pasted from Experimenter): opt in to
//   `optin_branch` of experiment with `optin_slug` from `optin_collection`.
//
// `--url file:///path/to/recipe.json?optin_branch=...` (as downloaded from
//   Experimenter): opt in to `optin_branch` of given experiment recipe.
//
// `--experiments file:///path/to/recipe.json` (as downloaded from
//   Experimenter): enable given experiment recipe, possibly enrolling into a
//   branch via regular bucketing.
//
// `--targeting-snapshot /path/to/targeting.snapshot.json`: take default profile
//   targeting information from given JSON file.
//
// `--reset-storage`: clear Activity Stream storage, including lifetime limit
//   caps.
//
// The following flags are intended for automated testing.
//
// --sentinel: bracket important output with given sentinel for easy parsing.
// --randomizationId: set the Nimbus profile group ID (the `group_id`
// randomization unit) for deterministic bucketing.
// --disable-alerts-service: do not show system/OS-level alerts.
// --no-experiments: don't talk to Remote Settings server at all.
// --no-datareporting: set `datareporting.healthreport.uploadEnabled=false` in
//   the background task profile.
// --no-optoutstudies: set `app.shield.optoutstudies.enabled=false` in the
//   background task profile.

import { EXIT_CODE } from "resource://gre/modules/BackgroundTasksManager.sys.mjs";
// eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
import { ASRouter } from "resource:///modules/asrouter/ASRouter.sys.mjs";
import { BackgroundTasksUtils } from "resource://gre/modules/BackgroundTasksUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ClientID: "resource://gre/modules/ClientID.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
  IndexedDB: "resource://gre/modules/IndexedDB.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  RemoteSettingsClient:
    "resource://services-settings/RemoteSettingsClient.sys.mjs",
  // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
  ToastNotification: "resource:///modules/asrouter/ToastNotification.sys.mjs",
  Utils: "resource://services-settings/Utils.sys.mjs",
});

const SERVER_STAGE = "https://firefox.settings.services.allizom.org/v1";

// Default profile targeting snapshot.
let defaultProfileTargetingSnapshot = {};

// Bracket important output with given sentinel for easy parsing.
let outputInfo;
outputInfo = (sentinel, info) => {
  dump(`${sentinel}${JSON.stringify(info)}${sentinel}\n`);
};

/**
 * Monkey patch RemoteSettings clients to prevent HTTP connections and return
 * static data from the Nimbus collections.
 *
 * @param {object} options
 * @param {object[]} options.experiments
 * The experiments to return from the Nimbus experiments collections.
 */
function monkeyPatchRemoteSettingsClients({ experiments = [] }) {
  const now = Date.now();

  const nimbusClients = lazy.ExperimentAPI._rsLoader.remoteSettingsClients;

  // We aren't actually syncing these collections. In order for Nimbus to accept
  // recipes from these collections, they *must* have non-null value returned by
  // `getLastModified`. Otherwise, Nimbus will reject the recipes.
  const getLastModified = () => Promise.resolve(now);
  nimbusClients.experiments.db.getLastModified = getLastModified;
  nimbusClients.secureExperiments.db.getLastModified = getLastModified;

  // Prevent RemoteSettings clients from making HTTP connections to the Remote
  // Settings server, which will cause test failures. Additionally, return
  // `experiments` from both Nimbus collections. These collections each only
  // support a subset of features -- the secureExperiments collection, e.g.,
  // only supports recipes that have at least one of
  // `newtabTrainhopAddonDeployment` and `prefFlips`, but the
  // `RemoteSettingsExperimentLoader` will filter out the recipes from each
  // collection based on their supported features.
  lazy.RemoteSettingsClient.prototype.get = async function (options = {}) {
    const responseData =
      this === nimbusClients.experiments ||
      this === nimbusClients.secureExperiments
        ? experiments
        : [];

    outputInfo({
      "RemoteSettingsClient.get": { options, response: { data: responseData } },
    });
    return responseData;
  };
}

async function handleCommandLine(commandLine) {
  const CASE_INSENSITIVE = false;

  // Output data over stdout for tests to consume and inspect.
  let sentinel = commandLine.handleFlagWithParam("sentinel", CASE_INSENSITIVE);
  outputInfo = outputInfo.bind(null, sentinel || "");

  // We always want `nimbus.debug=true` for `about:studies?...` URLs.
  Services.prefs.setBoolPref("nimbus.debug", true);

  // Maybe drive up logging for this testing task.
  Services.prefs.clearUserPref("services.settings.preview_enabled");
  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.asrouter.debugLogLevel"
  );
  Services.prefs.clearUserPref("messaging-system.log");
  Services.prefs.clearUserPref("services.settings.loglevel");
  Services.prefs.clearUserPref("toolkit.backgroundtasks.loglevel");
  if (commandLine.handleFlag("debug", CASE_INSENSITIVE)) {
    console.log("Saw --debug, making logging verbose");
    Services.prefs.setBoolPref("services.settings.preview_enabled", true);
    Services.prefs.setCharPref(
      "browser.newtabpage.activity-stream.asrouter.debugLogLevel",
      "debug"
    );
    Services.prefs.setCharPref("messaging-system.log", "debug");
    Services.prefs.setCharPref("services.settings.loglevel", "debug");
    Services.prefs.setCharPref("toolkit.backgroundtasks.loglevel", "debug");
  }

  Services.prefs.setCharPref(
    "browser.region.network.url",
    `data:application/json,{"country_code": "US"}`
  );

  // Always make alert service display message when showing an alert.
  // Optionally suppress actually showing OS-level alerts.
  let origAlertsService = lazy.ToastNotification.AlertsService;
  let disableAlertsService = commandLine.handleFlag(
    "disable-alerts-service",
    CASE_INSENSITIVE
  );
  if (disableAlertsService) {
    console.log("Saw --disable-alerts-service, not showing any alerts");
  }
  // Remove getter so that we can redefine property.
  delete lazy.ToastNotification.AlertsService;
  lazy.ToastNotification.AlertsService = {
    showAlert(...args) {
      outputInfo({ showAlert: { args } });
      if (!disableAlertsService) {
        origAlertsService.showAlert(...args);
      }
    },
  };

  let targetingSnapshotPath = commandLine.handleFlagWithParam(
    "targeting-snapshot",
    CASE_INSENSITIVE
  );
  if (targetingSnapshotPath) {
    defaultProfileTargetingSnapshot = await IOUtils.readJSON(
      targetingSnapshotPath
    );
    console.log(
      `Saw --targeting-snapshot, read snapshot from ${targetingSnapshotPath}`
    );
  }
  outputInfo({ defaultProfileTargetingSnapshot });

  lazy.RemoteSettings.enablePreviewMode(false);
  Services.prefs.clearUserPref(
    "messaging-system.rsexperimentloader.collection_id"
  );
  if (commandLine.handleFlag("preview", CASE_INSENSITIVE)) {
    console.log(
      `Saw --preview, invoking 'RemoteSettings.enablePreviewMode(true)' and ` +
        `setting 'messaging-system.rsexperimentloader.collection_id=\"nimbus-preview\"'`
    );
    lazy.RemoteSettings.enablePreviewMode(true);
    Services.prefs.setCharPref(
      "messaging-system.rsexperimentloader.collection_id",
      "nimbus-preview"
    );
  }

  Services.prefs.clearUserPref("services.settings.server");
  Services.prefs.clearUserPref("services.settings.load_dump");
  if (commandLine.handleFlag("stage", CASE_INSENSITIVE)) {
    console.log(
      `Saw --stage, setting 'services.settings.server="${SERVER_STAGE}"'`
    );
    Services.prefs.setCharPref("services.settings.server", SERVER_STAGE);
    Services.prefs.setBoolPref("services.settings.load_dump", false);

    if (lazy.Utils.SERVER_URL !== SERVER_STAGE) {
      throw new Error(
        "Pref services.settings.server ignored!" +
          "remember to set MOZ_REMOTE_SETTINGS_DEVTOOLS=1 in beta and release."
      );
    }
  }

  // Allow to override the Nimbus profile group ID with `--randomizationId ...`.
  let randomizationId = commandLine.handleFlagWithParam(
    "randomizationId",
    CASE_INSENSITIVE
  );
  if (randomizationId) {
    console.log(`Saw --randomizationId: ${randomizationId}`);
    try {
      await lazy.ClientID.setProfileGroupID(randomizationId);
    } catch (e) {
      console.error(e);
    }
  }
  outputInfo({
    randomizationId: await lazy.ClientID.getProfileGroupID(),
  });

  // Allow to override Nimbus experiments with `--experiments /path/to/data.json`.
  let experiments = commandLine.handleFlagWithParam(
    "experiments",
    CASE_INSENSITIVE
  );
  if (experiments) {
    let experimentsPath = commandLine.resolveFile(experiments).path;
    let data = await IOUtils.readJSON(experimentsPath);
    if (!Array.isArray(data)) {
      if (data.permissions) {
        data = data.data;
      }
      data = [data];
    }

    monkeyPatchRemoteSettingsClients({ experiments: data });

    console.log(`Saw --experiments, read recipes from ${experimentsPath}`);
  }

  // Allow to turn off querying Remote Settings entirely, for tests.
  if (
    !experiments &&
    commandLine.handleFlag("no-experiments", CASE_INSENSITIVE)
  ) {
    monkeyPatchRemoteSettingsClients({ experiments: [] });

    console.log(`Saw --no-experiments, returning [] recipes`);
  }

  // Allow to test various red buttons.
  Services.prefs.clearUserPref("datareporting.healthreport.uploadEnabled");
  if (commandLine.handleFlag("no-datareporting", CASE_INSENSITIVE)) {
    Services.prefs.setBoolPref(
      "datareporting.healthreport.uploadEnabled",
      false
    );
    console.log(
      `Saw --no-datareporting, set 'datareporting.healthreport.uploadEnabled=false'`
    );
  }

  Services.prefs.clearUserPref("app.shield.optoutstudies.enabled");
  if (commandLine.handleFlag("no-optoutstudies", CASE_INSENSITIVE)) {
    Services.prefs.setBoolPref("app.shield.optoutstudies.enabled", false);
    console.log(
      `Saw --no-datareporting, set 'app.shield.optoutstudies.enabled=false'`
    );
  }

  outputInfo({
    taskProfilePrefs: {
      "app.shield.optoutstudies.enabled": Services.prefs.getBoolPref(
        "app.shield.optoutstudies.enabled"
      ),
      "datareporting.healthreport.uploadEnabled": Services.prefs.getBoolPref(
        "datareporting.healthreport.uploadEnabled"
      ),
    },
  });

  if (commandLine.handleFlag("reset-storage", CASE_INSENSITIVE)) {
    console.log("Saw --reset-storage, deleting database 'ActivityStream'");
    console.log(
      `To hard reset, remove the contents of '${PathUtils.profileDir}'`
    );
    await lazy.IndexedDB.deleteDatabase("ActivityStream");
  }
}

export async function runBackgroundTask(commandLine) {
  console.error("runBackgroundTask: message");

  // Most of the task is arranging configuration.
  await handleCommandLine(commandLine);

  outputInfo({
    chromeColorSchemeIsDark: Services.appinfo.chromeColorSchemeIsDark,
    prefersReducedMotion: Services.appinfo.prefersReducedMotion,
  });

  // Here's where we actually start Nimbus and the Firefox Messaging
  // System.
  await BackgroundTasksUtils.enableNimbus(
    commandLine,
    defaultProfileTargetingSnapshot.environment
  );

  await BackgroundTasksUtils.enableFirefoxMessagingSystem(
    defaultProfileTargetingSnapshot.environment
  );

  // At the time of writing, toast notifications are torn down as the
  // process exits.  Give the user a chance to see the notification.
  await lazy.ExtensionUtils.promiseTimeout(1000);

  // Everything in `ASRouter` is asynchronous, so we need to give everything a
  // chance to complete.
  outputInfo({ ASRouterState: ASRouter.state });

  return EXIT_CODE.SUCCESS;
}
