/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Marionette / Selenium `executeAsyncScript` entry point for the
 * aiwindow-extended firefox inference runner. Dispatches the six
 * Smart-Window handlers (chat / memories / search-browsing-history)
 * locally and forwards everything else to the toolkit Runner.sys.mjs.
 * Dispatch logic lives in AiwindowRunner.sys.mjs so mochitests can
 * import the same handlers directly without depending on this
 * script-blob shape.
 */
{
  const done = arguments[arguments.length - 1];
  const callArgs = [...arguments].slice(0, arguments.length - 1);
  const { run } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/scripts/AiwindowRunner.sys.mjs"
  );
  run(done, callArgs);
}
