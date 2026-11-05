/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { TaskbarTabsPinBase } from "moz-src:///browser/components/taskbartabs/TaskbarTabsPinBase.sys.mjs";

let lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
});

/**
 * Pins Taskbar Tabs on MSIX Windows by requesting a secondary tile, since
 * the Start Menu folder used for shortcuts isn't accessible from MSIX.
 */
export class TaskbarTabsPinMsix extends TaskbarTabsPinBase {
  async createShortcut(aTaskbarTab, aIconFile, aArgs, _aWindow) {
    this._logConsole.info("Creating Taskbar Tabs secondary tile.");

    let secondaryTileId = "taskbartab-" + aTaskbarTab.id;
    await lazy.ShellService.requestCreateAndPinSecondaryTile(
      secondaryTileId,
      aTaskbarTab.name,
      aIconFile.path,
      aArgs
    );

    return secondaryTileId;
  }

  async deleteShortcut(aRelativePath) {
    await lazy.ShellService.requestDeleteSecondaryTile(aRelativePath);
  }
}
