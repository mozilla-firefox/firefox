/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Marionette / Selenium `executeAsyncScript` entry point for the firefox
 * inference runner. The harness wraps this file in a function and forwards
 * `[...callArgs, doneCallback]`. Dispatch logic lives in Runner.sys.mjs so
 * mochitests can import the same handlers directly without depending on this
 * script-blob shape.
 */
{
  const done = arguments[arguments.length - 1];
  const callArgs = [...arguments].slice(0, arguments.length - 1);
  const { run } = ChromeUtils.importESModule(
    "resource://gre/modules/firefox_inference/Runner.sys.mjs"
  );
  run(done, callArgs);
}
