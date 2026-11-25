/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This script is injected into Firefox's privileged chrome context by the Python
// driver. It imports the in-tree binding layer and forwards the command plus its
// arguments to handleMessage.

// Turn the injected arguments into a locally scoped variable:
/* eslint-disable no-undef */
/** @type {unknown[]} */
const injectedArgs = arguments;
/* eslint-enable no-undef */

// The last injected argument is the "done" callback supplied by
// execute_async_script, the rest are the command name and its arguments.
const done = injectedArgs[injectedArgs.length - 1];
const callArgs = [...injectedArgs].slice(0, -1);

/**
 * @param {unknown} rawError
 */
function serializeError(rawError) {
  if (rawError && typeof rawError === "object") {
    const { message, name, stack } = /** @type {Error} */ (rawError);
    return { message, name, stack };
  }
  return { message: String(rawError) };
}

(async () => {
  /** @type {import("../../../BrowserAIDriver.sys.mjs")} */
  let driver;
  try {
    driver = ChromeUtils.importESModule(
      "moz-src:///browser/components/ai/driver/BrowserAIDriver.sys.mjs"
    );
  } catch (error) {
    // The BrowserAIDriver is only loaded in Nightly, so it may not be present on
    // the current Firefox.
    done({
      name: "error",
      error: {
        message:
          "Could not load the AI driver binding layer, which is only shipped " +
          `on Nightly and local builds.\n${error}`,
      },
    });
    return;
  }

  try {
    done({ name: "success", result: await driver.handleMessage(...callArgs) });
  } catch (rawError) {
    done({ name: "error", error: serializeError(rawError) });
  }
})();
