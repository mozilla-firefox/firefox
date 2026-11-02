/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
});

const {
  PREFERENCES,
} = require("resource://devtools/client/aboutdebugging/src/constants.js");

exports.parseFileUri = function (url) {
  // Strip a leading slash from Windows drive letter URIs.
  // file:///home/foo ~> /home/foo
  // file:///C:/foo ~> C:/foo
  const windowsRegex = /^file:\/\/\/([a-zA-Z]:\/.*)/;
  if (windowsRegex.test(url)) {
    return windowsRegex.exec(url)[1];
  }
  return url.slice("file://".length);
};

exports.getExtensionUuid = function (extension) {
  const { manifestURL } = extension;
  // Strip off the protocol and rest, leaving us with just the UUID.
  return manifestURL ? /moz-extension:\/\/([^/]*)/.exec(manifestURL)[1] : null;
};

/**
 * Open a file picker to allow the user to locate a temporary extension. A temporary
 * extension can either be:
 * - a folder
 * - a .xpi file
 * - a .zip file
 *
 * @param  {Window} win
 *         The window object where the filepicker should be opened.
 *         Note: We cannot use the global window object here because it is undefined if
 *         this module is loaded from a file outside of devtools/client/aboutdebugging/.
 *         See browser-loader.sys.mjs `uri.startsWith(baseURI)` for more details.
 * @param  {string} message
 *         The help message that should be displayed to the user in the filepicker.
 * @return {Promise} returns a promise that resolves a File object corresponding to the
 *         file selected by the user.
 */
exports.openTemporaryExtension = function (win, message) {
  return new Promise(resolve => {
    const fp = Cc["@mozilla.org/filepicker;1"].createInstance(Ci.nsIFilePicker);
    fp.init(win.browsingContext, message, Ci.nsIFilePicker.modeOpen);

    // Try to set the last directory used as "displayDirectory".
    try {
      const lastDirPath = Services.prefs.getCharPref(
        PREFERENCES.TEMPORARY_EXTENSION_PATH,
        ""
      );
      let lastDir = new lazy.FileUtils.File(lastDirPath);
      if (lastDir.exists() && !lastDir.isDirectory()) {
        lastDir = lastDir.parent;
      }
      fp.displayDirectory = lastDir;
    } catch (e) {
      // Empty or invalid value, nothing to handle.
    }

    fp.open(res => {
      if (res == Ci.nsIFilePicker.returnCancel || !fp.file) {
        return;
      }
      let file = fp.file;
      // AddonManager.installTemporaryAddon accepts either
      // addon directory or final xpi file.
      if (
        !file.isDirectory() &&
        !file.leafName.endsWith(".xpi") &&
        !file.leafName.endsWith(".zip")
      ) {
        file = file.parent;
      }

      // We are about to resolve, store the path to the directory for the next
      // call.
      const dir = file.isDirectory() ? file : file.parent;
      Services.prefs.setCharPref(
        PREFERENCES.TEMPORARY_EXTENSION_PATH,
        dir.path
      );

      resolve(file);
    });
  });
};
