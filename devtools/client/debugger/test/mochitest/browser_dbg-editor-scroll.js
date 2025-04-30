/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// Tests that the editor keeps proper scroll position per document
// while also moving to the correct location upon pause/breakpoint selection

"use strict";

// This test runs too slowly on linux debug. I'd like to figure out
// which is the slowest part of this and make it run faster, but to
// fix a frequent failure allow a longer timeout.
requestLongerTimeout(2);

/**
 * Test some simple usecases where the editor should scroll to the paused location
 * and remember the previously scrolled location when switching between two distinct sources.
 */
add_task(async function testScrollingOnPauseAndSourceSwitching() {
  const dbg = await initDebugger(
    "doc-editor-scroll.html",
    "scroll.js",
    "long.js"
  );

  // Set the initial breakpoint.
  await selectSource(dbg, "scroll.js");
  await addBreakpoint(dbg, "scroll.js", 26);

  info("Open long file, scroll down to line below the fold");
  await selectSource(dbg, "long.js");

  // Make sure we have a real scroll event emitted.
  // scrollEditorIntoView will bail out after 500ms but here we need to
  // guarantee the scroll event was processed by CodeMirror, because this is
  // when the viewport location gets saved.
  const ensureScroll = waitForScrolling(dbg, { useTimeoutFallback: false });

  await scrollEditorIntoView(dbg, 25, 0);
  ok(isScrolledPositionVisible(dbg, 25), "Scroll position is visible");

  info("Wait for the codemirror scroll event");
  await ensureScroll;

  info("Ensure vertical scroll is the same after switching documents");
  let onScrolled = waitForScrolling(dbg);
  await selectSource(dbg, "scroll.js");
  // Wait for any codemirror editor scroll that can happen
  await onScrolled;
  ok(isScrolledPositionVisible(dbg, 1), "Scrolled to the top of the editor");

  onScrolled = waitForScrolling(dbg);
  await selectSource(dbg, "long.js");
  await onScrolled;
  ok(isScrolledPositionVisible(dbg, 25), "Scroll position is visible");

  info("Trigger a pause, click on a frame, ensure the right line is selected");
  onScrolled = waitForScrolling(dbg);
  invokeInTab("line26");
  await waitForPaused(dbg);
  await onScrolled;

  ok(
    isScrolledPositionVisible(dbg, 26),
    "Frame scrolled down to correct location"
  );

  info("Navigating while paused, goes to the correct location");
  onScrolled = waitForScrolling(dbg);
  await selectSource(dbg, "long.js");
  await onScrolled;
  ok(isScrolledPositionVisible(dbg, 25), "Scroll position is visible");

  info("Open new source, ensure it's at 0 scroll");
  onScrolled = waitForScrolling(dbg);
  await selectSource(dbg, "frames.js");
  await onScrolled;
  ok(isScrolledPositionVisible(dbg, 1), "Scrolled to the top of the editor");
});

/**
 * Some extensive test against Editor's isPositionVisible implementation.
 *
 * Assert precisely which lines are considered visible or hidden by this method,
 * while ensuring that the editor scrolls to the pause location in some edgecases.
 * For example, when the line is partially visible at the end of the viewport.
 */
add_task(async function testIsPositionVisible() {
  // Ensure having the default fixed height, as it can impact the number of displayed lines
  await pushPref("devtools.toolbox.footer.height", 250);

  // Also set a precise size for side panels, as it can impact the number of displayed columns
  await pushPref("devtools.debugger.start-panel-size", 300);
  await pushPref("devtools.debugger.end-panel-size", 300);

  // Strengthen the test by ensuring we always use the same Firefox window size.
  // Note that the inner size is the important one as that's the final space available for DevTools.
  // The outer size will be different based on OS/Environment.
  const expectedWidth = 1280;
  const expectedHeight = 1040;
  if (
    window.innerWidth != expectedWidth ||
    window.innerHeight != expectedHeight
  ) {
    info("Resize the top level window to match the expected size");
    const onResize = once(window, "resize");
    const deltaW = window.outerWidth - window.innerWidth;
    const deltaH = window.outerHeight - window.innerHeight;
    const originalWidth = window.outerWidth;
    const originalHeight = window.outerHeight;
    window.resizeTo(expectedWidth + deltaW, expectedHeight + deltaH);
    await onResize;
    registerCleanupFunction(() => {
      window.resizeTo(originalWidth, originalHeight);
    });
  }
  is(window.innerWidth, expectedWidth);

  const dbg = await initDebugger(
    "doc-editor-scroll.html",
    "scroll.js",
    "long.js"
  );

  await selectSource(dbg, "scroll.js");
  const editor = getCMEditor(dbg);

  // All the following methods lookup for first/last visible position in the current viewport.
  // Also note that the element at the returned position may only be partially visible.
  function getFirstVisibleLine() {
    const { x, y } = editor.codeMirror.dom.getBoundingClientRect();
    // Add a pixel as we may be on the edge of the previous line which is hidden
    const pos = editor.codeMirror.posAtCoords({ x, y: y + 1 });
    return editor.codeMirror.state.doc.lineAt(pos).number;
  }
  function getLastVisibleLine() {
    const { x, y, height } = editor.codeMirror.dom.getBoundingClientRect();
    const pos = editor.codeMirror.posAtCoords({ x, y: y + height });
    return editor.codeMirror.state.doc.lineAt(pos).number;
  }
  const lastLine = getLastVisibleLine();

  is(
    lastLine,
    11,
    "The last line is the 11th. (it may change if you resize the default browser window height)"
  );
  ok(isScrolledPositionVisible(dbg, 1), "First line is visible");
  ok(
    isScrolledPositionVisible(dbg, lastLine - 1),
    "The line before the last one is visible"
  );
  ok(
    !isScrolledPositionVisible(dbg, lastLine),
    "Last line is partially visible and considered hidden"
  );
  ok(
    !isScrolledPositionVisible(dbg, lastLine + 1),
    "The line after the last is hidden"
  );

  info(
    "Set a breakpoint and pause on the last fully visible line, it should not scroll"
  );
  await addBreakpoint(dbg, "scroll.js", lastLine - 1);
  invokeInTab("line" + (lastLine - 1));
  await waitForPaused(dbg);

  ok(
    !isScrolledPositionVisible(dbg, lastLine),
    "Last line, which is partially visible, is still hidden"
  );

  await resume(dbg);

  info(
    "Set a breakpoint on the last partially visible line, it should scroll that line in the middle of the viewport"
  );
  await addBreakpoint(dbg, "scroll.js", lastLine);
  invokeInTab("line" + lastLine);
  await waitForPaused(dbg);

  const newLastLine = getLastVisibleLine();
  is(newLastLine, 16, "The new last line is the 16th");
  ok(
    !isScrolledPositionVisible(dbg, newLastLine),
    "The new Last line is still partially visible and considered hidden"
  );
  ok(
    isScrolledPositionVisible(dbg, newLastLine - 1),
    "The line before is reported as visible"
  );
  const firstLine = getFirstVisibleLine();
  is(firstLine, 6);
  ok(
    isScrolledPositionVisible(dbg, firstLine),
    "The new first line is visible"
  );
  ok(
    !isScrolledPositionVisible(dbg, firstLine - 1),
    "The line before the new first line is hidden"
  );

  await resume(dbg);

  info(
    "Set a breakpoint far from the current position, it should also scroll and display the paused line in the middle of the viewport"
  );
  await addBreakpoint(dbg, "scroll.js", 50);
  invokeInTab("line50");
  await waitForPaused(dbg);

  const newLastLine2 = getLastVisibleLine();
  is(newLastLine2, 55);
  ok(
    !isScrolledPositionVisible(dbg, newLastLine2),
    "The new last line is partially visible and considered as hidden"
  );
  ok(
    isScrolledPositionVisible(dbg, newLastLine2 - 1),
    "The line before is visible"
  );
  const firstLine2 = getFirstVisibleLine();
  is(firstLine2, 45);
  ok(
    isScrolledPositionVisible(dbg, firstLine2),
    "The new first line is visible"
  );
  ok(
    !isScrolledPositionVisible(dbg, firstLine2 - 1),
    "The line before the new first line is hidden"
  );

  await resume(dbg);
});

add_task(async function testColumnBreakpointsLimitAfterVerticalScroll() {
  // Keep the layout consistent
  await pushPref("devtools.debugger.end-panel-size", 300);
  await pushPref("devtools.debugger.ui.editor-wrapping", true);

  const dbg = await initDebugger(
    "doc-large-sources.html",
    "codemirror-bundle.js"
  );

  info("Select the minified bundle and add a breakpoint");
  await selectSource(dbg, "codemirror-bundle.js");
  await addBreakpoint(dbg, "codemirror-bundle.js", 1);

  let columnBreakpointMarkers = await waitForAllElements(
    dbg,
    "columnBreakpoints"
  );

  is(
    columnBreakpointMarkers.length,
    100,
    "We have the expected limit of column breakpoint markers on the minified source"
  );

  info("Scroll to the bottom of the file");
  await scrollEditorIntoView(dbg, 1);

  columnBreakpointMarkers = findAllElements(dbg, "columnBreakpoints");
  is(
    columnBreakpointMarkers.length,
    0,
    "There are no column breakpoint markers as the source has vertically scrolled the viewport over the limit"
  );

  info("Scroll back to the top of the file");
  await scrollEditorIntoView(dbg, 0);

  columnBreakpointMarkers = await waitForAllElements(dbg, "columnBreakpoints");
  is(
    columnBreakpointMarkers.length,
    100,
    "We still have the expected limit of column breakpoint markers on the minified source"
  );
});
