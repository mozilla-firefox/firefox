/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { TaskbarTabsPinBase } from "moz-src:///browser/components/taskbartabs/TaskbarTabsPinBase.sys.mjs";

let lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
  TaskbarTabsUtils:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsUtils.sys.mjs",
});

/**
 * Pins Taskbar Tabs on Linux by creating a desktop entry.
 */
export class TaskbarTabsPinLinux extends TaskbarTabsPinBase {
  async createShortcut(aTaskbarTab, aIconFile, aArgs, _aWindow) {
    this._logConsole.info("Creating Taskbar Tabs desktop entry.");

    let appId = lazy.TaskbarTabsUtils._determineNewDesktopEntryName(
      aTaskbarTab.id
    );
    await lazy.ShellService.createLinuxDesktopEntry(
      appId,
      aTaskbarTab.name,
      aArgs,
      aIconFile.path
    );

    return appId + ".desktop";
  }

  async deleteShortcut(aRelativePath) {
    await lazy.ShellService.deleteLinuxDesktopEntry(
      aRelativePath.replace(/\.desktop$/, "")
    );
  }
}
