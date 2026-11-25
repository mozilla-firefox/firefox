/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// @ts-check

/**
 * Firefox Desktop half of the AI driver's binding layer.
 *
 * This module supplies what the app-independent half cannot: how to reach the
 * actor for the tab the user is looking at. Commands needing modules that live
 * only in `browser/` are added to its table here too. It is what the external
 * Python client (`mozai`) imports, and what the browser mochitest exercises
 * in-process.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import {
  geckoCommands,
  sessions,
  AI_DRIVER_API_VERSION,
} from "moz-src:///toolkit/components/ml/driver/AIDriver.sys.mjs";

/**
 * @import { DriverContext } from "moz-src:///toolkit/components/ml/driver/AIDriver.sys.mjs"
 */

const lazy = XPCOMUtils.declareLazy({
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
});

/**
 * Resolve a JSWindowActor for the selected tab of the top-most browser window.
 *
 * @param {string} name
 * @returns {any}
 */
function getActor(name) {
  const window = lazy.BrowserWindowTracker.getTopWindow();
  if (!window) {
    throw new Error("No browser window is available for the AI driver.");
  }

  const actor =
    window.gBrowser.selectedBrowser.browsingContext.currentWindowGlobal.getActor(
      name
    );

  if (!actor) {
    throw new Error(`${name} actor is not available for the selected tab.`);
  }

  return actor;
}

/**
 * The toolkit commands, plus any that need `browser/`.
 *
 * @type {Record<string, (ctx: DriverContext, ...args: any[]) => any>}
 */
export const commands = {
  ...geckoCommands,

  /**
   * Return all of the available commands.
   */
  describe() {
    return {
      apiVersion: AI_DRIVER_API_VERSION,
      commands: Object.keys(commands).sort(),
    };
  },
};

/**
 * @param {string} name
 * @param {...any} args
 * @returns {Promise<any>}
 */
export async function handleMessage(name, ...args) {
  if (!Object.hasOwn(commands, name)) {
    throw new Error(`Unknown message: ${name}`);
  }
  return commands[name]({ getActor, sessions }, ...args);
}
