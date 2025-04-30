/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// Test ensures zombie debug lines do not persist
// https://github.com/firefox-devtools/debugger/issues/7755

"use strict";

add_task(async function () {
  // Load test files
  const dbg = await initDebugger("doc-sources.html");
  await waitForSources(dbg, "simple1.js", "simple2.js");

  // Add breakpoint to debug-line-2
  await selectSource(dbg, "simple2.js");
  await addBreakpoint(dbg, "simple2.js", 5);

  // Trigger the breakpoint ane ensure we're paused
  invokeInTab("main");
  await waitForPaused(dbg);

  // Scroll element into view
  findElement(dbg, "frame", 2).focus();

  let pausedLocation = findElementWithSelector(dbg, ".paused-location");
  ok(
    pausedLocation.classList.contains("first-column"),
    "This paused caret is displayed as the first element in the line"
  );

  // Click the call stack to get to debugger-line-1
  const dispatched = waitForDispatch(dbg.store, "ADD_INLINE_PREVIEW");
  await clickElement(dbg, "frame", 2);
  await dispatched;
  await waitForRequestsToSettle(dbg);
  await waitForSelectedSource(dbg, "simple1.js");
  await waitForSelectedLocation(dbg, 4, 18);

  pausedLocation = findElementWithSelector(dbg, ".paused-location");
  ok(
    !pausedLocation.classList.contains("first-column"),
    "This paused caret is no longer displayed as the first element in the line"
  );

  // Resume, which ends all pausing and would trigger the problem
  await resume(dbg);

  // Select the source that had the initial debug line
  await selectSource(dbg, "simple2.js");

  info("Ensuring there's no zombie debug line");
  is(
    findAllElements(dbg, "pausedLine").length,
    0,
    "Debug line no longer exists!"
  );
});
