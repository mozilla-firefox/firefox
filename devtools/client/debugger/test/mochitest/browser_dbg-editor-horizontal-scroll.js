/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// Tests that the editor scrolls correctly when pausing on location that
// requires horizontal scrolling.

"use strict";

add_task(async function testHorizontalScrolling() {
  if (!isCm6Enabled) {
    ok(true, "This test is disabled on CM5");
    return;
  }

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

  await selectSource(dbg, "horizontal-scroll.js");
  const editor = getCMEditor(dbg);

  const global = editor.codeMirror.contentDOM.ownerGlobal;
  const font = new global.FontFace(
    "Ahem",
    "url(chrome://mochitests/content/browser/devtools/client/debugger/test/mochitest/examples/Ahem.ttf)"
  );
  const loadedFont = await font.load();
  global.document.fonts.add(loadedFont);

  is(global.devicePixelRatio, 1);
  is(global.browsingContext.top.window.devicePixelRatio, 1);
  global.browsingContext.top.overrideDPPX = 1;
  is(global.browsingContext.fullZoom, 1);
  is(global.browsingContext.textZoom, 1);

  // /!\ Change the Codemirror font to use a fixed font across all OSes
  // and always have the same number of characters displayed.
  // Note that this devtools mono makes the "o" characters almost invisible.
  editor.codeMirror.contentDOM.style.fontFamily = "Ahem";
  editor.codeMirror.contentDOM.style.fontSize = "10px";
  editor.codeMirror.contentDOM.style.lineHeight = "15px";
  editor.codeMirror.contentDOM.style.fontWeight = "normal";
  editor.codeMirror.contentDOM.style.fontStyle = "normal";
  editor.codeMirror.contentDOM.style.fontStretch = "normal";
  is(global.getComputedStyle(editor.codeMirror.contentDOM).fontFamily, "Ahem");

  await wait(1000);

  is(
    Math.round(editor.codeMirror.dom.getBoundingClientRect().width),
    679,
    "Sanity check to ensure we have a fixed editor width, so that we have the expected displayed columns"
  );

  // All the following methods lookup for first/last visible position in the current viewport.
  // Also note that the element at the returned position may only be partially visible.
  function getFirstVisibleColumn() {
    const { x, y } = editor.codeMirror.dom.getBoundingClientRect();
    const gutterWidth =
      editor.codeMirror.dom.querySelector(".cm-gutters").clientWidth;
    // This is hardcoded to match the second line, which is around 20px from the top.
    // Also append the gutter width as it would pick hidden columns displayed behind it
    const pos = editor.codeMirror.posAtCoords({
      x: x + gutterWidth + 2,
      y: y + 20,
    });
    // /!\ the column is 0-based while lines are 1-based
    return pos - editor.codeMirror.state.doc.lineAt(pos).from;
  }
  function getLastVisibleColumn() {
    const { x, y, width } = editor.codeMirror.dom.getBoundingClientRect();
    // This is hardcoded to match the second line, which is around 20px from the top
    const pos = editor.codeMirror.posAtCoords({ x: x + width, y: y + 20 });
    // /!\ the column is 0-based while lines are 1-based
    return pos - editor.codeMirror.state.doc.lineAt(pos).from;
  }

  info("Pause in middle of the screen, we should not scroll on pause");
  await addBreakpoint(dbg, "horizontal-scroll.js", 2, 25);
  invokeInTab("horizontal");
  await waitForPaused(dbg);

  const lastColumn = getLastVisibleColumn();
  is(lastColumn, 54);
  ok(
    isScrolledPositionVisible(dbg, 2, 1),
    "The 2nd line, first column is visible"
  );
  ok(
    isScrolledPositionVisible(dbg, 2, lastColumn),
    "The 2nd line, last column is visible"
  );
  ok(
    !isScrolledPositionVisible(dbg, 2, lastColumn + 1),
    "The column after the last column is hidden"
  );

  info("Step to the last visible column, the editor shouldn't scroll");
  // There is one breakable position every two column.
  // We can see the column breakpoint for the column after the last visible one.
  // Setting a breakpoint on that column and pausing shouldn't cause to scroll the viewport.
  await addBreakpoint(dbg, "horizontal-scroll.js", 2, lastColumn + 1);
  await resume(dbg);
  await waitForPaused(dbg);

  is(getLastVisibleColumn(), lastColumn, "We did not scroll horizontaly");
  ok(
    isScrolledPositionVisible(dbg, 2, lastColumn),
    "The last column is still visible"
  );
  ok(
    !isScrolledPositionVisible(dbg, 2, lastColumn + 1),
    "The column after the last colunm is still hidden"
  );

  info(
    "Step to the next column, and the editor should scroll it into the center"
  );

  info("Step into the next breakable column, the editor should now scroll");
  // Set a breakpoint on the first column that would cause a scroll
  // (there is one breakable position every two column)
  await addBreakpoint(dbg, "horizontal-scroll.js", 2, lastColumn + 3);
  await resume(dbg);
  await waitForPaused(dbg);

  const lastColumn2 = getLastVisibleColumn();

  is(lastColumn2, 74);
  ok(
    isScrolledPositionVisible(dbg, 2, lastColumn2),
    "The new last column is visible"
  );
  ok(
    !isScrolledPositionVisible(dbg, 2, lastColumn2 + 1),
    "The column after the last is hidden"
  );
  const firstColumn = getFirstVisibleColumn();
  is(firstColumn, 30);
  ok(
    !isScrolledPositionVisible(dbg, 2, firstColumn),
    "The new first column is partially visible and considered hidden"
  );
  ok(
    isScrolledPositionVisible(dbg, 2, firstColumn + 1),
    "The column after the first visible is visible"
  );

  await resume(dbg);
});

// Tests the limit of the no of column breakpoint markers in a minified source with a long line.
add_task(async function testColumnBreakpointsLimitAfterHorizontalScroll() {
  // Keep the layout consistent
  await pushPref("devtools.debugger.end-panel-size", 300);

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

  info("Scroll horizintally far to the right of the file");
  await scrollEditorIntoView(dbg, 0, 300000);

  columnBreakpointMarkers = findAllElements(dbg, "columnBreakpoints");
  is(
    columnBreakpointMarkers.length,
    0,
    "There are no column breakpoint marker as the source has horizontally scrolled the viewport over the limit"
  );

  info("Scroll back to the start of the line");
  await scrollEditorIntoView(dbg, 0, 0);

  columnBreakpointMarkers = await waitForAllElements(dbg, "columnBreakpoints");
  is(
    columnBreakpointMarkers.length,
    100,
    "We still have the expected limit of column breakpoint markers on the minified source"
  );
});
