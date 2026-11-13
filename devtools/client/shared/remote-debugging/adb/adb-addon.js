/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { AddonManager } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs",
  // AddonManager is a singleton, never create two instances of it.
  { global: "shared" }
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const EventEmitter = require("resource://devtools/shared/event-emitter.js");

const PREF_ADB_EXTENSION_URL = "devtools.remote.adb.extensionURL";
const PREF_ADB_EXTENSION_ID = "devtools.remote.adb.extensionID";

const ADB_ADDON_STATES = {
  DOWNLOADING: "downloading",
  INSTALLED: "installed",
  INSTALLING: "installing",
  PREPARING: "preparing",
  UNINSTALLED: "uninstalled",
  UNKNOWN: "unknown",
};
exports.ADB_ADDON_STATES = ADB_ADDON_STATES;

/**
 * Wrapper around the ADB Extension providing ADB binaries for devtools remote debugging.
 * Fires the following events:
 * - "update": the status of the addon was updated
 * - "failure": addon installation failed
 * - "progress": addon download in progress
 *
 * AdbAddon::state can take any of the values from ADB_ADDON_STATES.
 */
class ADBAddon extends EventEmitter {
  constructor() {
    super();

    this._status = ADB_ADDON_STATES.UNKNOWN;

    const addonsListener = {};
    addonsListener.onEnabled =
      addonsListener.onDisabled =
      addonsListener.onInstalled =
      addonsListener.onUninstalled =
        () => this.updateInstallStatus();
    AddonManager.addAddonListener(addonsListener);

    this.updateInstallStatus();
  }

  set status(value) {
    if (this._status != value) {
      this._status = value;
      this.emit("update");
    }
  }

  get status() {
    return this._status;
  }

  async _getAddon(addonId = Services.prefs.getCharPref(PREF_ADB_EXTENSION_ID)) {
    return AddonManager.getAddonByID(addonId);
  }

  async updateInstallStatus() {
    const addon = await this._getAddon();
    if (addon && !addon.userDisabled) {
      this.status = ADB_ADDON_STATES.INSTALLED;
    } else {
      this.status = ADB_ADDON_STATES.UNINSTALLED;
    }
  }

  /**
   * Returns the platform specific download link for the ADB extension.
   */
  _getXpiLink() {
    let OS = "";

    switch (AppConstants.platform) {
      case "linux": {
        const cpuArch = Services.sysinfo.get("arch");
        if (cpuArch === "x86-64") {
          OS = "linux64";
        } else {
          OS = "linux";
        }
        break;
      }
      case "macosx":
        OS = "mac64";
        break;
      case "win":
        OS = "win32";
        break;
    }

    const xpiLink = Services.prefs.getCharPref(PREF_ADB_EXTENSION_URL);
    return xpiLink.replace(/#OS#/g, OS);
  }

  /**
   * Install and enable the adb extension. Returns a promise that resolves when ADB is
   * enabled.
   *
   * @param {string} source
   *        String passed to the AddonManager for telemetry.
   */
  async install(source) {
    if (!source) {
      throw new Error(
        "Missing mandatory `source` parameter for adb-addon.install"
      );
    }

    const addonId = Services.prefs.getCharPref(PREF_ADB_EXTENSION_ID);
    const addon = await this._getAddon(addonId);
    if (addon && !addon.userDisabled) {
      this.status = ADB_ADDON_STATES.INSTALLED;
      return;
    }
    this.status = ADB_ADDON_STATES.PREPARING;
    if (addon?.userDisabled) {
      await addon.enable();
    } else {
      const install = await AddonManager.getInstallForURL(this._getXpiLink(), {
        id: addonId,
        telemetryInfo: { source },
        type: "extension",
      });
      install.addListener(this);
      install.install();
    }
  }

  async uninstall() {
    const addon = await this._getAddon();
    addon.uninstall();
  }

  installFailureHandler(install, message) {
    this.status = ADB_ADDON_STATES.UNINSTALLED;
    this.emit("failure", message);
  }

  // Expected AddonManager install listener.
  onDownloadStarted() {
    this.status = ADB_ADDON_STATES.DOWNLOADING;
  }

  // Expected AddonManager install listener.
  onDownloadProgress(install) {
    if (install.maxProgress == -1) {
      this.emit("progress", -1);
    } else {
      this.emit("progress", install.progress / install.maxProgress);
    }
  }

  // Expected AddonManager install listener.
  onDownloadCancelled(install) {
    this.installFailureHandler(install, "Download cancelled");
  }

  // Expected AddonManager install listener.
  onDownloadFailed(install) {
    this.installFailureHandler(install, "Download failed");
  }

  // Expected AddonManager install listener.
  onInstallStarted() {
    this.status = ADB_ADDON_STATES.INSTALLING;
  }

  // Expected AddonManager install listener.
  onInstallCancelled(install) {
    this.installFailureHandler(install, "Install cancelled");
  }

  // Expected AddonManager install listener.
  onInstallFailed(install) {
    this.installFailureHandler(install, "Install failed");
  }

  // Expected AddonManager install listener.
  onInstallEnded({ addon }) {
    addon.enable();
  }
}

exports.adbAddon = new ADBAddon();
