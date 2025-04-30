/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

"use strict";

// Tests that breakpoint panels open when their relevant breakpoint is hit
add_task(async function testBreakpointsPaneOpenOnPause() {
  const dbg = await initDebugger("doc-sources.html", "simple1.js");

  clickElementWithSelector(dbg, ".breakpoints-pane h2");

  info("Confirm the breakpoints pane is closed");
  is(getPaneElements(dbg).length, 1);

  await selectSource(dbg, "simple1.js");
  await addBreakpoint(dbg, "simple1.js", 4);

  info("Trigger the breakpoint ane ensure we're paused");
  invokeInTab("main");
  await waitForPaused(dbg, "simple1.js");

  info("Confirm the breakpoints pane is open");
  is(getPaneElements(dbg).length, 2);

  await resume(dbg);

  info("Confirm the breakpoints pane is closed again");
  is(getPaneElements(dbg).length, 1);
});

// Tests that the breakpoint pane remains closed on stepping
add_task(async function testBreakpointsPanePersistOnStepping() {
  const dbg = await initDebugger("doc-scripts.html", "simple3.js");

  await selectSource(dbg, "simple3.js");
  await addBreakpoint(dbg, "simple3.js", 9);
  await waitForBreakpoint(dbg, "simple3.js", 9);

  invokeInTab("nestedA");
  await waitForPaused(dbg, "simple3.js");

  is(getPaneElements(dbg).length, 2, "Breakpoints pane is open");

  info("Close breakpoints pane");

  clickElementWithSelector(dbg, ".breakpoints-pane h2");

  is(getPaneElements(dbg).length, 1, "Breakpoint pane is closed");

  info("Step into nestedB");

  await stepIn(dbg);

  const frameElements = findAllElements(dbg, "frames");
  assertFrameIsSelected(dbg, frameElements[0], "nestedB");

  is(getPaneElements(dbg).length, 1, "Breakpoints pane is still closed");
});

// Tests that the breakpoint pane remains closed on call-stack frame selection
add_task(async function testBreakpointsPanePersistOnFrameSelection() {
  const dbg = await initDebugger("doc-scripts.html", "simple3.js");

  info("Close breakpoints pane");

  clickElementWithSelector(dbg, ".breakpoints-pane h2");

  is(getPaneElements(dbg).length, 1, "Breakpoint pane is closed");

  await selectSource(dbg, "simple3.js");
  await addBreakpoint(dbg, "simple3.js", 13);
  await waitForBreakpoint(dbg, "simple3.js", 13);

  invokeInTab("nestedA");
  await waitForPaused(dbg, "simple3.js");

  is(getPaneElements(dbg).length, 2, "Breakpoints pane is open");

  info("Close breakpoints pane");

  clickElementWithSelector(dbg, ".breakpoints-pane h2");

  is(getPaneElements(dbg).length, 1, "Breakpoint pane is closed");

  is(getFrameList(dbg).length, 2, "Call stack has two frames");

  info("Click on second call stack frame");

  await clickElement(dbg, "frame", 2);

  const frameElements = findAllElements(dbg, "frames");
  assertFrameIsSelected(dbg, frameElements[1], "nestedA");

  is(getPaneElements(dbg).length, 1, "Breakpoint pane is still closed");
});

// Tests that the breakpoint pane persists when toggled during pause
add_task(async function testBreakpointsPanePersistOnPauseToggle() {
  const dbg = await initDebugger("doc-scripts.html", "simple3.js");

  await selectSource(dbg, "simple3.js");
  await addBreakpoint(dbg, "simple3.js", 9);
  await waitForBreakpoint(dbg, "simple3.js", 9);

  info("Close breakpoint pane");

  clickElementWithSelector(dbg, ".breakpoints-pane h2");

  is(getPaneElements(dbg).length, 1, "Breakpoint pane is closed");

  info("Trigger breakpoint");

  invokeInTab("nestedA");
  await waitForPaused(dbg, "simple3.js");

  is(getPaneElements(dbg).length, 2, "Breakpoints pane is open");

  info("Close breakpoint pane and reopen it");

  clickElementWithSelector(dbg, ".breakpoints-pane h2");

  is(getPaneElements(dbg).length, 1, "Breakpoints pane is closed");

  clickElementWithSelector(dbg, ".breakpoints-pane h2");

  is(getPaneElements(dbg).length, 2, "Breakpoints pane is open");

  await resume(dbg);

  // After resuming from the breakpoint, the debugger statement will be hit.
  await waitForPaused(dbg, "simple3.js");
  await resume(dbg);

  is(getPaneElements(dbg).length, 2, "Breakpoints pane is still open");
});

// Tests that the breakpoint pane remains closed when event breakpoints log is toggled
add_task(async function testBreakpointsPaneRemainsClosedWhenLogToggled() {
  const dbg = await initDebugger("doc-scripts.html", "simple3.js");

  await selectSource(dbg, "simple3.js");
  await addBreakpoint(dbg, "simple3.js", 9);
  await waitForBreakpoint(dbg, "simple3.js", 9);

  info("Trigger breakpoint");

  invokeInTab("nestedA");
  await waitForPaused(dbg, "simple3.js");

  info("Close breakpoint pane");

  clickElementWithSelector(dbg, ".breakpoints-pane h2");

  is(getPaneElements(dbg).length, 1, "Breakpoint pane is closed");

  info("Check event listener breakpoints log box");
  const wait = waitForDispatch(dbg.store, "TOGGLE_EVENT_LISTENERS");
  await clickElement(dbg, "logEventsCheckbox");
  await wait;

  is(getPaneElements(dbg).length, 1, "Breakpoint pane is still closed");
});

function getPaneElements(dbg) {
  return findElementWithSelector(dbg, ".breakpoints-pane").childNodes;
}

function getFrameList(dbg) {
  return dbg.win.document.querySelectorAll(".call-stack-pane .frame");
}
