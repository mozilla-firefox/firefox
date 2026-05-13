/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { run } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/scripts/AiwindowRunner.sys.mjs"
);

/**
 * Route a single named operation through `run`. Resolves to the envelope
 * the dispatcher produces (`{ name: "success", result }` or
 * `{ name: "error", error }`) — the same shape ml_driver receives over
 * the Marionette wire.
 *
 * @param {...*} args - `[name, ...handlerArgs]`
 * @returns {Promise<object>}
 */
function dispatch(...args) {
  return new Promise(resolve => run(resolve, args));
}
