/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// Test that service worker sources are still displayed after reloading the page
// and that we can hit breakpoints in them.

"use strict";

const SW_URL = EXAMPLE_URL + "service-worker.sjs";

add_task(async function () {
  await pushPref("devtools.debugger.threads-visible", true);
  await pushPref("dom.serviceWorkers.testing.enabled", true);

  const dbg = await initDebugger("doc-service-workers.html");

  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    await content.wrappedJSObject.registerWorker();
  });
  const workerSource = await waitForSource(dbg, "service-worker.sjs");

  await reload(dbg, "service-worker.sjs");

  await addBreakpoint(dbg, "service-worker.sjs", 13);
  const onFetched = invokeInTab("fetchFromWorker");

  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, workerSource.id, 13);

  await assertPreviews(dbg, [
    {
      line: 10,
      column: 9,
      result: EXAMPLE_URL + "whatever",
      expression: "url",
    },
  ]);

  await resume(dbg);
  info("Waiting for the fetch request done from the page to complete");
  await onFetched;

  // Reload a second time to ensure we can still debug the SW
  await reload(dbg, "service-worker.sjs");

  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, workerSource.id, 13);

  await assertPreviews(dbg, [
    {
      line: 10,
      column: 9,
      result: EXAMPLE_URL + "whatever",
      expression: "url",
    },
  ]);

  await resume(dbg);

  await unregisterServiceWorker(SW_URL);

  await checkAdditionalThreadCount(dbg, 0);

  await waitForRequestsToSettle(dbg);

  await closeTabAndToolbox();
});
