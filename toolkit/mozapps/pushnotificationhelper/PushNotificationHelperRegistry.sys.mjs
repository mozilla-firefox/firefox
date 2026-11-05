/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "log", () =>
  console.createInstance({
    maxLogLevel: "Error",
    maxLogLevelPref: "app.backgroundNotifications.helper.loglevel",
    logModule: "PushNotificationHelper",
  })
);

// Separates the installation and profile directories in the registry value name
const REGISTRY_VALUE_NAME_SEPARATOR = "|";

/**
 * Build the registry value name.
 *
 * @param {string} installDir - Install directory.
 * @param {string} profileDir - Profile directory.
 * @returns {string} Registry value name.
 */
export function buildRegistryValueName(installDir, profileDir) {
  return installDir + REGISTRY_VALUE_NAME_SEPARATOR + profileDir;
}

/**
 * Split the registry value name into the install and profile directories.
 *
 * @param {string} name - Registry value name.
 * @returns {{installDir: string, profileDir: string}|null} The directories.
 */
function splitRegistryValueName(name) {
  const at = name.lastIndexOf(REGISTRY_VALUE_NAME_SEPARATOR);
  if (at < 0) {
    return null;
  }
  return { installDir: name.slice(0, at), profileDir: name.slice(at + 1) };
}

/**
 * Build the registry key path.
 *
 * @returns {string} Registry key path.
 */
export function buildRegistryKeyPath() {
  const vendor = Services.appinfo.vendor || "Mozilla";
  return `Software\\${vendor}\\${Services.appinfo.name}\\Notification Helper`;
}

/**
 * Open the registry key with the list of push notification helpers running,
 * pass it to a function and close it afterwards.
 *
 * @param {function(nsIWindowsRegKey): *} func - Function using the key.
 * @param {object} [options]
 * @param {boolean} [options.create] - Whether to create the key when it is
 *   missing. Otherwise, a missing key throws.
 * @returns {*} The value returned by the function.
 */
export function withRegistryKey(func, { create = false } = {}) {
  const key = Cc["@mozilla.org/windows-registry-key;1"].createInstance(
    Ci.nsIWindowsRegKey
  );
  const mode = key.ACCESS_READ | key.ACCESS_WRITE | key.WOW64_64;
  if (create) {
    key.create(key.ROOT_KEY_CURRENT_USER, buildRegistryKeyPath(), mode);
  } else {
    key.open(key.ROOT_KEY_CURRENT_USER, buildRegistryKeyPath(), mode);
  }
  try {
    return func(key);
  } finally {
    key.close();
  }
}

/**
 * Check if a directory path exists.
 *
 * @param {string} dirPath - Directory path.
 * @returns {boolean} True if the directory exists, false otherwise.
 */
function dirExists(dirPath) {
  try {
    const dir = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
    dir.initWithPath(dirPath);
    return dir.exists();
  } catch (e) {
    return false;
  }
}

/**
 * The registry record of the running push notification helpers, one value per
 * helper under HKCU, shared by every install of this user. Value names are
 * "<install directory>|<profile directory>", following the shape of the
 * Default Browser Agent key. The updater reads this record to restart the
 * helpers it stops before applying an update.
 */
export const PushNotificationHelperRegistry = {
  /**
   * Add a push notification helper record.
   *
   * @param {string} installDir - Install directory.
   * @param {string} profileDir - Profile directory.
   */
  add(installDir, profileDir) {
    try {
      withRegistryKey(
        key =>
          key.writeIntValue(buildRegistryValueName(installDir, profileDir), 1),
        { create: true }
      );
    } catch (e) {
      lazy.log.error(
        "Failed to add the push notification helper record to the registry",
        e
      );
    }
  },

  /**
   * Remove a push notification helper record.
   *
   * @param {string} installDir - Install directory.
   * @param {string} profileDir - Profile directory.
   */
  remove(installDir, profileDir) {
    const name = buildRegistryValueName(installDir, profileDir);
    try {
      withRegistryKey(key => {
        if (key.hasValue(name)) {
          key.removeValue(name);
        }
      });
    } catch (e) {
      // The key does not exist, so there is nothing to remove
    }
  },

  /**
   * Check if a push notification helper is recorded.
   *
   * @param {string} installDir - Install directory.
   * @param {string} profileDir - Profile directory.
   * @returns {boolean} True if the push notification helper is recorded.
   */
  has(installDir, profileDir) {
    try {
      return withRegistryKey(key =>
        key.hasValue(buildRegistryValueName(installDir, profileDir))
      );
    } catch (e) {
      return false;
    }
  },

  /**
   * Remove the push notification helper records of an install whose profile
   * directory no longer exists.
   *
   * @param {string} installDir - Install directory.
   */
  prune(installDir) {
    try {
      withRegistryKey(key => {
        // Backwards, because removing a value shifts the indexes after it
        for (let i = key.valueCount - 1; i >= 0; i--) {
          const name = key.getValueName(i);
          const parts = splitRegistryValueName(name);
          if (
            parts?.installDir.toLowerCase() == installDir.toLowerCase() &&
            !dirExists(parts.profileDir)
          ) {
            key.removeValue(name);
          }
        }
      });
    } catch (e) {
      // The key does not exist, so there is nothing to prune
    }
  },

  /**
   * Get the install directories of the push notification helpers of a profile.
   *
   * The same profile can be used by push notification helpers from different
   * installations.
   *
   * @param {string} profileDir - Profile directory.
   * @returns {string[]} Install directories.
   */
  getInstallDirsForProfile(profileDir) {
    const installDirs = [];

    try {
      withRegistryKey(key => {
        for (let i = 0; i < key.valueCount; i++) {
          const name = key.getValueName(i);
          const parts = splitRegistryValueName(name);
          if (parts?.profileDir.toLowerCase() == profileDir.toLowerCase()) {
            installDirs.push(parts.installDir);
          }
        }
      });
    } catch (e) {
      // The key does not exist, so there is nothing recorded
    }

    return installDirs;
  },
};
