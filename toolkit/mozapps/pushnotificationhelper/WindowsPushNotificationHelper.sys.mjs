/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const BINARY_NAME = "notification-helper.exe";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PushNotificationHelperRegistry:
    "resource://gre/modules/PushNotificationHelperRegistry.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () =>
  console.createInstance({
    maxLogLevel: "Error",
    maxLogLevelPref: "app.backgroundNotifications.helper.loglevel",
    logModule: "PushNotificationHelper",
  })
);

/**
 * Launches the helper.
 *
 * @param {string[]} args - full argument list for the helper.
 * @returns {boolean} whether the helper was launched.
 */
function launch(args) {
  const exe = Services.dirsvc.get("GreBinD", Ci.nsIFile);
  exe.append(BINARY_NAME);

  const process = createProcess(exe);
  if (!process) {
    return false;
  }

  process.runw(false, args, args.length);

  return true;
}

/**
 * Prepares to run the helper at `exe`.
 *
 * @param {nsIFile} exe - Helper executable.
 * @returns {nsIProcess|null} The process, or null when the helper is missing.
 */
function createProcess(exe) {
  if (!exe.exists()) {
    lazy.log.error(`${exe.path} is not installed`);
    return null;
  }

  const process = Cc["@mozilla.org/process/util;1"].createInstance(
    Ci.nsIProcess
  );

  process.init(exe);
  process.startHidden = true;
  process.noShell = true;

  return process;
}

/**
 * Runs the helper of the install at `installDir` and waits for it to exit.
 *
 * @param {string} installDir - Install directory holding the helper.
 * @param {string[]} args - Full argument list for the helper.
 * @returns {Promise<boolean>} Whether the helper ran and exited.
 */
function launchAndWait(installDir, args) {
  const exe = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
  exe.initWithPath(installDir);
  exe.append(BINARY_NAME);

  const process = createProcess(exe);
  if (!process) {
    return Promise.resolve(false);
  }

  return new Promise(resolve => {
    process.runwAsync(args, args.length, {
      observe(subject, topic) {
        resolve(topic == "process-finished");
      },
    });
  });
}

/**
 * Windows implementation of the push notification helper. Whether a helper
 * should be running is decided by PushNotificationHelper.sys.mjs; this module
 * only knows how to act on that decision.
 */
export const WindowsPushNotificationHelper = {
  get installDir() {
    return Services.dirsvc.get("GreBinD", Ci.nsIFile).path;
  },

  get profileDir() {
    return Services.dirsvc.get("ProfD", Ci.nsIFile).path;
  },

  /**
   * Names the profile a helper serves. A helper is per profile, so this is the
   * whole of its identity.
   *
   * @returns {string[]} arguments naming the profile.
   */
  get profileArgs() {
    return ["--profile", this.profileDir];
  },

  /**
   * Starts the push notification helper for the current running profile and
   * records it in the registry.
   */
  start() {
    if (launch(this.profileArgs)) {
      lazy.PushNotificationHelperRegistry.add(this.installDir, this.profileDir);
    }
    lazy.PushNotificationHelperRegistry.prune(this.installDir);
  },

  /**
   * Asks this profile's helper to exit, including one left behind by an
   * earlier Firefox session, and removes it from the registry. Does nothing
   * when no helper was recorded, so a profile that never had one does not
   * launch a --stop process on every startup.
   */
  stop() {
    if (
      lazy.PushNotificationHelperRegistry.has(this.installDir, this.profileDir)
    ) {
      launch(["--stop", ...this.profileArgs]);
      lazy.PushNotificationHelperRegistry.remove(
        this.installDir,
        this.profileDir
      );
    }
    lazy.PushNotificationHelperRegistry.prune(this.installDir);
  },

  /**
   * Stops all helpers for a profile, from any installation, and removes them
   * from the registry.
   *
   * Intended for a profile that is about to be removed, so this must run before
   * any of its files are deleted.
   *
   * A helper is found using the profile's path, which can no longer be resolved
   * once the directory is deleted.
   *
   * @param {string} profileDir - Profile directory.
   */
  async stopForProfile(profileDir) {
    const installDirs =
      lazy.PushNotificationHelperRegistry.getInstallDirsForProfile(profileDir);

    for (const installDir of installDirs) {
      try {
        await launchAndWait(installDir, ["--stop", "--profile", profileDir]);
      } catch (e) {
        lazy.log.error("Failed to stop the helper", e);
      }
      lazy.PushNotificationHelperRegistry.remove(installDir, profileDir);
    }
  },
};
