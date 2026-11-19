/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const codeCoverage = Cc["@mozilla.org/tools/code-coverage;1"].getService(
  Ci.nsICodeCoverage
);
const it = task => add_task(task);

it(async function test_flush_resolves_when_a_content_process_dies() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "https://example.com/", forceNewProcess: true },
    async browser => {
      // Start the flush before the parent observes the crash.
      const crashed = BrowserTestUtils.crashFrame(browser);
      try {
        Assert.strictEqual(
          await codeCoverage.flushCounters(),
          undefined,
          "The flush resolves when a process dies"
        );
      } finally {
        await crashed;
      }
    }
  );
});
