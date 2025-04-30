/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { Actor } = require("resource://devtools/shared/protocol.js");
const {
  addonsSpec,
} = require("resource://devtools/shared/specs/addon/addons.js");

const { AddonManager } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs",
  { global: "shared" }
);
const { FileUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/FileUtils.sys.mjs",
  { global: "contextual" }
);

// This actor is used by DevTools as well as external tools such as webext-run
// and the Firefox VS-Code plugin. see bug #1578108
class AddonsActor extends Actor {
  constructor(conn) {
    super(conn, addonsSpec);
  }

  async installTemporaryAddon(addonPath, openDevTools) {
    let addonFile;
    let addon;
    try {
      addonFile = new FileUtils.File(addonPath);
      addon = await AddonManager.installTemporaryAddon(addonFile);
    } catch (error) {
      let msg = `${error}`;
      if (error.additionalErrors?.length) {
        // The generic "Extension is invalid" error does not offer any concrete
        // advice. Include the additional details to help with debugging.
        msg += `\n${error.additionalErrors.join("\n")}`;
      }
      throw new Error(`Could not install add-on at '${addonPath}': ${msg}`);
    }

    Services.obs.notifyObservers(null, "devtools-installed-addon", addon.id);

    // Try to open DevTools for the installed add-on.
    // Note that it will only work on Firefox Desktop.
    // On Android, we don't ship DevTools UI.
    // about:debugging is only using this API when debugging its own firefox instance,
    // so for now, there is no chance of calling this on Android.
    if (openDevTools) {
      // This module is typically loaded in the loader spawn by DevToolsStartup,
      // in a distinct compartment thanks to useDistinctSystemPrincipalLoader
      // and global flag.
      // But here we want to reuse the shared module loader.
      // We do not want to load devtools.js in the server's distinct module loader.
      const loader = ChromeUtils.importESModule(
        "resource://devtools/shared/loader/Loader.sys.mjs",
        { global: "shared" }
      );
      const {
        gDevTools,
        // eslint-disable-next-line mozilla/reject-some-requires
      } = loader.require("resource://devtools/client/framework/devtools.js");
      gDevTools.showToolboxForWebExtension(addon.id);
    }

    // TODO: once the add-on actor has been refactored to use
    // protocol.js, we could return it directly.
    // return new AddonTargetActor(this.conn, addon);

    // Return a pseudo add-on object that a calling client can work
    // with. Provide a flag that the client can use to detect when it
    // gets upgraded to a real actor object.
    return { id: addon.id, actor: false };
  }

  async uninstallAddon(addonId) {
    const addon = await AddonManager.getAddonByID(addonId);

    // We only support uninstallation of temporarily loaded add-ons at the
    // moment.
    if (!addon?.temporarilyInstalled) {
      throw new Error(`Could not uninstall add-on "${addonId}"`);
    }

    await addon.uninstall();
  }
}

exports.AddonsActor = AddonsActor;
