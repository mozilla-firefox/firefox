/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { TaskbarTabsPinBase } from "moz-src:///browser/components/taskbartabs/TaskbarTabsPinBase.sys.mjs";

let lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
});

/**
 * Pins Taskbar Tabs on non-MSIX Windows by creating a shortcut in the Start
 * Menu and pinning it to the taskbar.
 */
export class TaskbarTabsPinWin extends TaskbarTabsPinBase {
  async createShortcut(aTaskbarTab, aIconFile, aArgs, _aWindow) {
    this._logConsole.info("Creating Taskbar Tabs shortcut.");

    let { relativePath, description } =
      await this.#generateShortcutInfo(aTaskbarTab);
    this._logConsole.debug(
      `Using shortcut path relative to Programs folder: ${relativePath}`
    );

    await lazy.ShellService.createShortcut(
      Services.dirsvc.get("XREExeF", Ci.nsIFile),
      aArgs,
      description,
      aIconFile,
      0,
      aTaskbarTab.id, // AUMID
      "Programs",
      relativePath
    );

    return relativePath;
  }

  async pinShortcut(aTaskbarTab, aShortcut) {
    let pinResult = await lazy.ShellService.pinShortcutToTaskbar(
      aTaskbarTab.id,
      "Programs",
      aShortcut
    );
    return pinResult === Ci.nsIWindowsShellService.REJECTED;
  }

  unpinFromTaskbar(aRelativePath) {
    lazy.ShellService.unpinShortcutFromTaskbar("Programs", aRelativePath);
  }

  async deleteShortcut(aRelativePath) {
    await lazy.ShellService.deleteShortcut("Programs", aRelativePath);
  }

  /**
   * Gets a Localization object to use when creating shortcuts.
   *
   * @returns {Localization} An object to access localized strings.
   */
  static _getLocalization() {
    return new Localization(["branding/brand.ftl", "browser/taskbartabs.ftl"]);
  }

  /**
   * Gets the path to the shortcut relative to the Start Menu folder,
   * as well as the description that should be attached to the shortcut.
   *
   * @param {TaskbarTab} aTaskbarTab - The Taskbar Tab to get the path of.
   * @returns {Promise<{description: string, relativePath: string}>} The description
   * and relative path of the shortcut.
   */
  async #generateShortcutInfo(aTaskbarTab) {
    const l10n = TaskbarTabsPinWin._getLocalization();

    let basename = sanitizeFilename(aTaskbarTab.name + ".lnk");
    let dirname = await l10n.formatValue("taskbar-tab-shortcut-folder");
    dirname = sanitizeFilename(dirname, { allowDirectoryNames: true });

    const description = await l10n.formatValue(
      "taskbar-tab-shortcut-description",
      { name: aTaskbarTab.name }
    );

    return {
      description,
      relativePath: dirname + "\\" + basename,
    };
  }
}

/**
 * Cleans up the filename so it can be saved safely. This means replacing invalid names
 * (e.g. DOS devices) with others, or replacing invalid characters (e.g. asterisks on
 * Windows) with underscores.
 *
 * @param {string} aWantedName - The name to validate and sanitize. Make sure
 * that aWantedName has an extension if it will be saved with one.
 * @param {object} aOptions - Options to affect the sanitization.
 * @param {boolean} aOptions.allowDirectoryNames - Indicates that the name will be used
 * as a directory. If so, the validation rules may be slightly more lax.
 * @returns {string} The sanitized name.
 */
function sanitizeFilename(aWantedName, { allowDirectoryNames = false } = {}) {
  const mimeService = Cc["@mozilla.org/mime;1"].getService(Ci.nsIMIMEService);

  let flags =
    Ci.nsIMIMEService.VALIDATE_SANITIZE_ONLY |
    Ci.nsIMIMEService.VALIDATE_DONT_COLLAPSE_WHITESPACE |
    // Don't add .download to the name if it ends with .lnk.
    Ci.nsIMIMEService.VALIDATE_ALLOW_INVALID_FILENAMES;

  if (allowDirectoryNames) {
    flags |= Ci.nsIMIMEService.VALIDATE_ALLOW_DIRECTORY_NAMES;
  }

  return mimeService.validateFileNameForSaving(aWantedName, "", flags);
}
