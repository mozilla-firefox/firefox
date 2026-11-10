/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

/*
 * Test reloading:
 * 1. reload the source
 * 2. re-sync breakpoints
 */

"use strict";

// `findSource` in shared-head.js matches a bare filename or a full URL, and the
// ember bundle has 18 distinct `index.js` original sources, so this one has to
// be named by its URL.
const INDEX_URL = `${EXAMPLE_URL}ember/quickstart/dist/assets/ember-application/index.js`;

add_task(async function () {
  // Without the script navigation cache, reloading the bundle can hand the
  // debugger a source-map mapping to an original source the sources reducer
  // has not been told about yet, which throws and permanently loses the
  // reloaded sources. The pref is Nightly-only, so the test only hits this on
  // beta and release builds.
  await pushPref("dom.script_loader.experimental.navigation_cache", true);

  const dbg = await initDebugger("ember/quickstart/dist/", INDEX_URL);

  await selectSource(dbg, INDEX_URL);

  info("1. reload and hit breakpoint");
  await addBreakpoint(dbg, INDEX_URL, 4);
  // Deliberately not awaited: the next reload has to start while the previous
  // one is still processing sources. They are collected so that step 5 can
  // settle them. An abandoned reload stays suspended on a page load the
  // breakpoint holds, and its frame keeps `dbg`, and with it the toolbox
  // window, alive until shutdown.
  const reloads = [reload(dbg)];

  info("2. Wait for sources to appear and then reload");
  // Wait for the script and style soources to be added
  await waitForDispatch(dbg.store, "ADD_SOURCES", 2);
  reloads.push(reload(dbg));

  info("3. Wait for sources to appear and then reload mid source-maps");
  await waitForDispatch(dbg.store, "ADD_SOURCES", 2);
  reloads.push(reload(dbg));

  info(
    "4. wait for the debugger to pause and show that we're in the correct location"
  );
  // Original variable mapping is off, so no scope is displayed here.
  await waitForPaused(dbg, null, { shouldWaitForLoadedScopes: false });
  const source = findSource(dbg, INDEX_URL);
  await assertPausedAtSourceAndLine(dbg, source.id, 4);

  info("5. resume so the pending reloads can complete");
  await removeBreakpoint(dbg, source.id, 4);
  await resume(dbg);
  await Promise.all(reloads);
});
