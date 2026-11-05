/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

let lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", () => {
  return console.createInstance({
    prefix: "TaskbarTabs",
    maxLogLevel: "Warn",
  });
});

/**
 * Integrates Taskbar Tabs with the desktop environment of the current
 * platform. Subclasses implement the platform-specific parts; this class is
 * used as-is on platforms where pinning isn't supported.
 *
 * Pinning a Taskbar Tab means calling createShortcut and then pinShortcut.
 * These are separate steps so that the shortcut is still known, and can be
 * deleted later, if pinning fails after the shortcut was created.
 */
export class TaskbarTabsPinBase {
  /**
   * The console used by subclasses to log messages.
   *
   * @type {ConsoleInstance}
   */
  get _logConsole() {
    return lazy.logConsole;
  }

  /**
   * Creates a shortcut that opens the Taskbar Tab. On some platforms, this
   * also pins it to the taskbar.
   *
   * @param {TaskbarTab} _aTaskbarTab - The Taskbar Tab to create a shortcut for.
   * @param {nsIFile} _aIconFile - The icon file to use for the shortcut.
   * @param {string[]} _aArgs - The command line arguments the shortcut should
   * pass to Firefox.
   * @param {?DOMWindow} _aWindow - The window to associate any UI with, or null
   * if no window is available.
   * @returns {Promise<string>} The path to the created shortcut.
   */
  async createShortcut(_aTaskbarTab, _aIconFile, _aArgs, _aWindow) {
    throw new Error("Don't know how to create a shortcut on this platform");
  }

  /**
   * Pins a shortcut created by createShortcut to the taskbar, if the platform
   * requires doing so separately from creating it.
   *
   * @param {TaskbarTab} _aTaskbarTab - The Taskbar Tab the shortcut opens.
   * @param {string} _aShortcut - The path to the shortcut, as returned by
   * createShortcut.
   * @returns {Promise<boolean>} Whether the user or system rejected the pin
   * request.
   */
  async pinShortcut(_aTaskbarTab, _aShortcut) {
    return false;
  }

  /**
   * Unpins a shortcut from the taskbar, if the platform requires doing so
   * separately from deleting it.
   *
   * @param {string} _aRelativePath - The path to the shortcut, as returned by
   * createShortcut.
   */
  unpinFromTaskbar(_aRelativePath) {}

  /**
   * Deletes a shortcut created by createShortcut.
   *
   * @param {string} _aRelativePath - The path to the shortcut, as returned by
   * createShortcut.
   * @returns {Promise} Resolves once the shortcut has been deleted.
   */
  async deleteShortcut(_aRelativePath) {}
}
