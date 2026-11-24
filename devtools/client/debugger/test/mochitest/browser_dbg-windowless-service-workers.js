/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// Test that we can detect a new service worker and hit breakpoints that we've
// set in it.

"use strict";

let SW_URL;

add_task(async function () {
  info("Subtest #1");
  await pushPref("devtools.debugger.threads-visible", true);
  await pushPref("dom.serviceWorkers.testing.enabled", true);

  // Use an URL with a specific port to check that Bug 1876533 is fixed
  // We're using URL without port in other tests, like browser_dbg-windowless-service-workers-reload.js
  const dbg = await initDebuggerWithAbsoluteURL(
    EXAMPLE_URL_WITH_PORT + "doc-service-workers.html"
  );

  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    await content.wrappedJSObject.registerWorker();
  });
  const workerSource = await waitForSource(dbg, "service-worker.sjs");

  // Retrieve the live URL of the Service Worker as it should be "http://" URL.
  // (based on EXAMPLE_URL_WITH_PORT URL http: + custom port)
  // But when running with --use-http3-server, the worker has an https URL.
  SW_URL = workerSource.url;

  await addBreakpoint(dbg, "service-worker.sjs", 13);

  const onFetched = invokeInTab("fetchFromWorker");

  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, workerSource.id, 13);
  // Leave the breakpoint and worker in place for the next subtest.
  await resume(dbg);
  info("Waiting for the fetch request done from the page to complete");
  await onFetched;
  await waitForRequestsToSettle(dbg);
  await closeTabAndToolbox();
});

// Test that breakpoints can be immediately hit in service workers when reloading.
add_task(async function () {
  info("Subtest #2");

  const toolbox = await openNewTabAndToolbox(
    `${EXAMPLE_URL_WITH_PORT}doc-service-workers.html`,
    "jsdebugger"
  );
  const dbg = createDebuggerContext(toolbox);

  await checkAdditionalThreadCount(dbg, 1);

  // The test page will immediately fetch from the service worker if registered.
  const onReloaded = reload(dbg);

  const workerSource = await waitForSource(dbg, "service-worker.sjs");

  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, workerSource.id, 13);
  await checkAdditionalThreadCount(dbg, 1);

  await resume(dbg);
  await dbg.actions.removeAllBreakpoints();

  info("Wait for reload to complete after resume");
  await onReloaded;

  await unregisterServiceWorker(SW_URL);

  await checkAdditionalThreadCount(dbg, 0);
  await waitForRequestsToSettle(dbg);

  await closeTabAndToolbox();
});

// Test having a waiting and active service worker for the same registration.
add_task(async function () {
  info("Subtest #3");

  const toolbox = await openNewTabAndToolbox(
    `${EXAMPLE_URL_WITH_PORT}doc-service-workers.html`,
    "jsdebugger"
  );
  const dbg = createDebuggerContext(toolbox);

  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    await content.wrappedJSObject.registerWorker();
  });
  await checkAdditionalThreadCount(dbg, 1);
  await checkWorkerStatus(dbg, "activated");

  const firstTab = gBrowser.selectedTab;

  await addTab(
    `${EXAMPLE_URL_WITH_PORT}service-worker.sjs?setStatus=newServiceWorker`
  );
  await removeTab(gBrowser.selectedTab);

  const secondTab = await addTab(
    `${EXAMPLE_URL_WITH_PORT}doc-service-workers.html`
  );

  await gBrowser.selectTabAtIndex(gBrowser.tabs.indexOf(firstTab));
  await checkAdditionalThreadCount(dbg, 2);

  const sources = await waitFor(() => {
    const list = dbg.selectors
      .getSourceList()
      .filter(s => s.url.includes("service-worker.sjs"));
    return list.length == 1 ? list : null;
  });
  ok(sources.length, "Found one sources for service worker");

  // Add a breakpoint for the next subtest.
  await addBreakpoint(dbg, "service-worker.sjs", 2);

  await unregisterServiceWorker(SW_URL);

  await checkAdditionalThreadCount(dbg, 0);
  await waitForRequestsToSettle(dbg);
  await removeTab(firstTab);
  await removeTab(secondTab);

  // Reset the SJS in case we will be repeating the test.
  await addTab(`${EXAMPLE_URL_WITH_PORT}service-worker.sjs?setStatus=`);
  await removeTab(gBrowser.selectedTab);
});

// Test setting breakpoints while the service worker is starting up.
add_task(async function () {
  info("Subtest #4");
  if (Services.appinfo.fissionAutostart) {
    // Disabled when serviceworker isolation is used due to bug 1749341
    return;
  }

  const toolbox = await openNewTabAndToolbox(
    `${EXAMPLE_URL_WITH_PORT}doc-service-workers.html`,
    "jsdebugger"
  );
  const dbg = createDebuggerContext(toolbox);

  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    await content.wrappedJSObject.registerWorker();
  });
  await checkAdditionalThreadCount(dbg, 1);

  const workerSource = await waitForSource(dbg, "service-worker.sjs");

  await waitForBreakpointCount(dbg, 1);
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, workerSource.id, 2);
  await checkWorkerStatus(dbg, "parsed");

  await addBreakpoint(dbg, "service-worker.sjs", 19);
  await resume(dbg);
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, workerSource.id, 19);
  await checkWorkerStatus(dbg, "installing");

  await addBreakpoint(dbg, "service-worker.sjs", 5);
  await resume(dbg);
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, workerSource.id, 5);
  await checkWorkerStatus(dbg, "activating");

  await resume(dbg);
  await unregisterServiceWorker(SW_URL);

  await checkAdditionalThreadCount(dbg, 0);
  await waitForRequestsToSettle(dbg);

  await closeTabAndToolbox();
});

async function checkWorkerStatus(_dbg, _status) {
  /* TODO: Re-Add support for showing service worker status (Bug 1641099)
  await waitUntil(() => {
    const threads = dbg.selectors.getThreads();
    return threads.some(t => t.serviceWorkerStatus == status);
  });
  ok(true, `Have thread with status ${status}`);
  */
}
