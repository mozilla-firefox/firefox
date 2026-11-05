/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

let lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
  TaskbarTabsUtils:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () => {
  return console.createInstance({
    logModule: "TaskbarTabs",
    maxLogLevel: "Warn",
  });
});

ChromeUtils.defineLazyGetter(lazy, "platformPin", () => {
  if (AppConstants.platform === "win" && lazy.TaskbarTabsUtils.isMSIX()) {
    const { TaskbarTabsPinMsix } = ChromeUtils.importESModule(
      "moz-src:///browser/components/taskbartabs/TaskbarTabsPinMsix.sys.mjs"
    );
    return new TaskbarTabsPinMsix();
  }

  if (AppConstants.platform === "win") {
    const { TaskbarTabsPinWin } = ChromeUtils.importESModule(
      "moz-src:///browser/components/taskbartabs/TaskbarTabsPinWin.sys.mjs"
    );
    return new TaskbarTabsPinWin();
  }

  if (AppConstants.platform === "linux") {
    const { TaskbarTabsPinLinux } = ChromeUtils.importESModule(
      "moz-src:///browser/components/taskbartabs/TaskbarTabsPinLinux.sys.mjs"
    );
    return new TaskbarTabsPinLinux();
  }

  const { TaskbarTabsPinBase } = ChromeUtils.importESModule(
    "moz-src:///browser/components/taskbartabs/TaskbarTabsPinBase.sys.mjs"
  );
  return new TaskbarTabsPinBase();
});

/**
 * Provides functionality to pin and unping Taskbar Tabs.
 */
export const TaskbarTabsPin = {
  /**
   * Pins the provided Taskbar Tab to the taskbar.
   *
   * @param {TaskbarTab} aTaskbarTab - A Taskbar Tab to pin to the taskbar.
   * @param {imgIContainer} aIcon - The icon to show with this Taskbar Tab.
   * @param {object} [aDetails] - Additional options for this pin.
   * @param {DOMWindow} [aDetails.window] - The window to associate any UI with.
   * @returns {Promise<{shortcut: ?string, rejected: boolean}>} The path to the
   * created shortcut, and whether the user or system rejected the pin request.
   */
  async pinTaskbarTab(aTaskbarTab, aIcon, { window = null } = {}) {
    lazy.logConsole.info("Pinning Taskbar Tab to the taskbar.");

    let shortcut = null;
    let rejected = false;
    try {
      let iconFile = await createTaskbarIcon(aTaskbarTab, aIcon);

      shortcut = await lazy.platformPin.createShortcut(
        aTaskbarTab,
        iconFile,
        getShortcutArgs(aTaskbarTab),
        window
      );
      rejected = await lazy.platformPin.pinShortcut(aTaskbarTab, shortcut);
      Glean.webApp.pin.record({ result: rejected ? "Rejected" : "Success" });
    } catch (e) {
      lazy.logConsole.error(`An error occurred while pinning: ${e.message}`);
      Glean.webApp.pin.record({ result: e.name ?? "Unknown exception" });
    }

    return { shortcut, rejected };
  },

  /**
   * Unpins the provided Taskbar Tab from the taskbar.
   *
   * @param {TaskbarTab} aTaskbarTab - The Taskbar Tab to unpin from the taskbar.
   * @returns {Promise} Resolves once finished.
   */
  async unpinTaskbarTab(aTaskbarTab) {
    let unpinError = null;
    let removalError = null;

    try {
      lazy.logConsole.info("Unpinning Taskbar Tab from the taskbar.");

      let relativePath = aTaskbarTab.shortcutRelativePath;

      try {
        lazy.platformPin.unpinFromTaskbar(relativePath);
      } catch (e) {
        lazy.logConsole.error(`Failed to unpin shortcut: ${e.message}`);
        unpinError = e;
      }

      let iconFile = getIconFile(aTaskbarTab);

      lazy.logConsole.debug(`Deleting ${relativePath}`);
      lazy.logConsole.debug(`Deleting ${iconFile.path}`);

      let deleteShortcut = relativePath
        ? lazy.platformPin.deleteShortcut(relativePath)
        : Promise.resolve();

      await Promise.all([deleteShortcut, IOUtils.remove(iconFile.path)]);
    } catch (e) {
      lazy.logConsole.error(
        `An error occurred while uninstalling: ${e.message}`
      );
      removalError = e;
    }

    const message = e => (e ? (e.name ?? "Unknown exception") : "Success");
    Glean.webApp.unpin.record({
      result: message(unpinError),
      removal_result: message(removalError),
    });
  },
};

/**
 * Fetches the favicon from the provided Taskbar Tab's start url, and saves it
 * to an icon file.
 *
 * @param {TaskbarTab} aTaskbarTab - The Taskbar Tab to generate an icon file for.
 * @param {imgIContainer} aIcon - The icon to associate with this Taskbar Tab.
 * @returns {Promise<nsIFile>} The created icon file.
 */
async function createTaskbarIcon(aTaskbarTab, aIcon) {
  lazy.logConsole.info("Creating Taskbar Tabs shortcut icon.");

  let iconFile = getIconFile(aTaskbarTab);

  lazy.logConsole.debug(`Using icon path: ${iconFile.path}`);

  await IOUtils.makeDirectory(iconFile.parent.path);

  await lazy.ShellService.writeShortcutIcon(iconFile, aIcon);

  return iconFile;
}

/**
 * Gets the command line arguments a shortcut should pass to Firefox to open
 * the provided Taskbar Tab.
 *
 * @param {TaskbarTab} aTaskbarTab - The Taskbar Tab the shortcut opens.
 * @returns {string[]} The command line arguments.
 */
function getShortcutArgs(aTaskbarTab) {
  let profileFolder = Services.dirsvc.get("ProfD", Ci.nsIFile);

  return [
    "-taskbar-tab",
    aTaskbarTab.id,
    "-new-window",
    aTaskbarTab.startUrl, // In case Taskbar Tabs is disabled, provide fallback url.
    "-profile",
    profileFolder.path,
    "-container",
    aTaskbarTab.userContextId.toString(),
  ];
}

/**
 * Generates a file path to use for the Taskbar Tab icon file.
 *
 * @param {TaskbarTab} aTaskbarTab - A Taskbar Tab to generate an icon file path for.
 * @returns {nsIFile} The icon file path for the Taskbar Tab.
 */
function getIconFile(aTaskbarTab) {
  let iconPath = lazy.TaskbarTabsUtils.getTaskbarTabsFolder();
  iconPath.append("icons");
  iconPath.append(
    aTaskbarTab.id + "." + lazy.ShellService.shortcutIconType.extension
  );
  return iconPath;
}
