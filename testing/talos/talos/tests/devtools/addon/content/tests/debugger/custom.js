/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  closeToolboxAndLog,
  garbageCollect,
  runTest,
  testSetup,
  testTeardown,
  PAGES_BASE_URL,
  waitForDOMElement,
} = require("../head");
const {
  createContext,
  findSource,
  getCMEditor,
  hoverOnToken,
  openDebuggerAndLog,
  pauseDebugger,
  reloadDebuggerAndLog,
  removeBreakpoints,
  resume,
  selectSource,
  step,
  waitForSource,
  waitForText,
  waitUntil,
  addBreakpoint,
  waitForPaused,
  waitForState,
  isCm6Enabled,
  openEditorContextMenu,
  selectEditorContextMenuItem,
  scrollEditorIntoView,
} = require("./debugger-helpers");

const IFRAME_BASE_URL =
  "http://damp.top.com/tests/devtools/addon/content/pages/";
const EXPECTED = {
  sources: 1149,
  file: "App.js",
  sourceURL: `${IFRAME_BASE_URL}custom/debugger/app-build/static/js/App.js`,
  text: "import React, { Component } from 'react';",
  threadsCount: 2,
};

const EXPECTED_FUNCTION = "window.hitBreakpoint()";

const TEST_URL = PAGES_BASE_URL + "custom/debugger/app-build/index.html";
const MINIFIED_URL = `${IFRAME_BASE_URL}custom/debugger/app-build/static/js/minified.js`;
const MAIN_URL = `${IFRAME_BASE_URL}custom/debugger/app-build/static/js/main.js`;

/*
 * See testing/talos/talos/tests/devtools/addon/content/pages/custom/debugger/app/src for the details
 * about the pages used for these tests.
 */
const STEP_TESTS = [
  // This steps only once from the App.js into step-in-test.js.
  // This `stepInNewSource` should always run first to make sure `step-in-test.js` file
  // is loaded for the first time.
  {
    stepCount: 1,
    location: { line: 22, file: "App.js" },
    key: "stepInNewSource",
    stepType: "stepIn",
  },
  {
    stepCount: 2,
    location: { line: 10194, file: "js/step-in-test.js" },
    key: "stepIn",
    stepType: "stepIn",
  },
  {
    stepCount: 2,
    location: { line: 16, file: "js/step-over-test.js" },
    key: "stepOver",
    stepType: "stepOver",
  },
  {
    stepCount: 2,
    location: { line: 998, file: "js/step-out-test.js" },
    key: "stepOut",
    stepType: "stepOut",
  },
];

module.exports = async function () {
  const tab = await testSetup(TEST_URL, { disableCache: true });

  const toolbox = await openDebuggerAndLog("custom", EXPECTED);

  dump("Waiting for debugger panel\n");
  const panel = await toolbox.getPanelWhenReady("jsdebugger");

  dump("Creating context\n");
  const dbg = await createContext(panel);

  // Reselect App.js as that's the source expected to be selected after page reload
  await selectSource(dbg, EXPECTED.file);

  await reloadDebuggerAndLog("custom", toolbox, EXPECTED);

  // these tests are only run on custom.jsdebugger
  await pauseDebuggerAndLog(dbg, tab, EXPECTED_FUNCTION);
  await stepDebuggerAndLog(dbg, tab, EXPECTED_FUNCTION, STEP_TESTS);

  await testProjectSearch(dbg, tab);
  await testPreview(dbg, tab, EXPECTED_FUNCTION);
  await testOpeningLargeMinifiedFile(dbg);
  await testPrettyPrint(dbg, toolbox);
  await testLargeFileWithWrapping(dbg, toolbox, tab);

  await testBigBundle(dbg, tab);

  await closeToolboxAndLog("custom.jsdebugger", toolbox);

  await testTeardown();
};

async function pauseDebuggerAndLog(dbg, tab, testFunction) {
  const pauseLocation = { line: 22, file: "App.js" };

  dump("Pausing debugger\n");
  let test = runTest("custom.jsdebugger.pause.DAMP");
  await pauseDebugger(dbg, tab, testFunction, pauseLocation);
  test.done();

  await removeBreakpoints(dbg);
  await resume(dbg);
  await garbageCollect();
}

async function stepDebuggerAndLog(dbg, tab, testFunction, stepTests) {
  for (const stepTest of stepTests) {
    await pauseDebugger(dbg, tab, testFunction, stepTest.location);
    const test = runTest(`custom.jsdebugger.${stepTest.key}.DAMP`);
    for (let i = 0; i < stepTest.stepCount; i++) {
      await step(dbg, stepTest.stepType);
    }
    test.done();
    await removeBreakpoints(dbg);
    await resume(dbg);
    await garbageCollect();
  }
}

async function testProjectSearch(dbg) {
  dump("Executing project search\n");
  const test = runTest(`custom.jsdebugger.project-search.DAMP`);
  const firstSearchResultTest = runTest(
    `custom.jsdebugger.project-search.first-search-result.DAMP`
  );
  await dbg.actions.setPrimaryPaneTab("project");
  await dbg.actions.setActiveSearch("project");
  const searchInput = await waitForDOMElement(
    dbg.win.document.querySelector("body"),
    ".project-text-search .search-field input"
  );
  searchInput.focus();
  searchInput.value = "retur";
  // Only dispatch a true key event for the last character in order to trigger only one search
  const key = "n";
  searchInput.dispatchEvent(
    new dbg.win.KeyboardEvent("keydown", {
      bubbles: true,
      cancelable: true,
      view: dbg.win,
      charCode: key.charCodeAt(0),
    })
  );
  searchInput.dispatchEvent(
    new dbg.win.KeyboardEvent("keyup", {
      bubbles: true,
      cancelable: true,
      view: dbg.win,
      charCode: key.charCodeAt(0),
    })
  );
  searchInput.dispatchEvent(
    new dbg.win.KeyboardEvent("keypress", {
      bubbles: true,
      cancelable: true,
      view: dbg.win,
      charCode: key.charCodeAt(0),
    })
  );

  // Wait till the first search result match is rendered
  await waitForDOMElement(
    dbg.win.document.querySelector("body"),
    ".project-text-search .tree-node .result"
  );
  firstSearchResultTest.done();
  // Then wait for all results to be fetched and the loader spin to hide
  await waitUntil(() => {
    return !dbg.win.document.querySelector(
      ".project-text-search .search-field .loader.spin"
    );
  });
  await dbg.actions.closeActiveSearch();
  test.done();
  await garbageCollect();
}

async function testPreview(dbg, tab, testFunction) {
  dump("Executing preview test ...\n");
  const pauseLocation = { line: 22, file: "App.js" };

  let test = runTest("custom.jsdebugger.preview.DAMP");
  await pauseDebugger(dbg, tab, testFunction, pauseLocation);
  await hoverOnToken(dbg, "window.hitBreakpoint", "window");
  test.done();

  await removeBreakpoints(dbg);
  await resume(dbg);
  await garbageCollect();
}

async function testOpeningLargeMinifiedFile(dbg) {
  dump("Executing opening large minified test ...\n");
  const fileFirstMinifiedChars = `(()=>{var e,t,n,r,o={82603`;

  dump("Open minified.js (large minified file)\n");
  const fullTest = runTest(
    "custom.jsdebugger.open-large-minified-file.full-selection.DAMP"
  );
  const test = runTest("custom.jsdebugger.open-large-minified-file.DAMP");
  const onSelected = selectSource(dbg, MINIFIED_URL);
  await waitForText(dbg, fileFirstMinifiedChars);
  test.done();
  await onSelected;
  fullTest.done();

  await dbg.actions.closeTabs([findSource(dbg, MINIFIED_URL)]);

  // Also clear to prevent reselecting this source
  await dbg.actions.clearSelectedLocation();

  await garbageCollect();
}

async function testLargeFileWithWrapping(dbg, toolbox) {
  await selectSource(dbg, MAIN_URL);
  dump("Turn on editor wrapping \n");
  await openEditorContextMenu(dbg, toolbox);
  await selectEditorContextMenuItem(dbg, toolbox, "editor-wrapping");
  await waitUntil(() => {
    if (isCm6Enabled()) {
      return dbg.win.document
        .querySelector(".cm-content")
        .classList.contains("cm-lineWrapping");
    }
    // Waiting for the white-space property value to change from "pre" (before line wrapping) to
    // "pre-wrap" (after line wrapping).
    return (
      dbg.win
        .getComputedStyle(dbg.win.document.querySelector(".CodeMirror-line"))
        .getPropertyValue("white-space") === "pre-wrap"
    );
  });

  dump("Add breakpoint to main.js with wrap editor switched on\n");
  const testBreakpoint = runTest(
    "custom.jsdebugger.with-wrap-editor.add-breakpoint.DAMP"
  );
  await addBreakpoint(dbg, 1, MAIN_URL);
  testBreakpoint.done();

  dump("Scroll main.js with wrap editor switched on\n");
  const testScroll = runTest("custom.jsdebugger.with-wrap-editor.scroll.DAMP");
  // Scroll the document until line 2 becomes visible (which is also the bottom of the document)
  await scrollEditorIntoView(dbg, 2);
  testScroll.done();

  dump("Turn off editor wrapping \n");
  await openEditorContextMenu(dbg, toolbox);
  await selectEditorContextMenuItem(dbg, toolbox, "editor-wrapping");
  await waitUntil(
    () => !Services.prefs.getBoolPref("devtools.debugger.ui.editor-wrapping")
  );

  await removeBreakpoints(dbg);
  await dbg.actions.closeTabs([findSource(dbg, MAIN_URL)]);

  await garbageCollect();
}

async function testPrettyPrint(dbg, toolbox) {
  const formattedFileUrl = `${MINIFIED_URL}:formatted`;
  const filePrettyChars = "82603: (e, t, n) => {\n";

  dump("Select minified file\n");
  await selectSource(dbg, MINIFIED_URL);

  dump("Wait until CodeMirror highlighting is done\n");
  const cm = getCMEditor(dbg).codeMirror;
  await waitUntil(() => {
    if (isCm6Enabled()) {
      return true;
    }
    // For CM5 highlightFrontier is not documented but is an internal variable indicating the current
    // line that was just highlighted. This document has only 2 lines, so wait until both
    // are highlighted. Since there was an other document opened before, we need to do an
    // exact check to properly wait.
    return cm.doc.highlightFrontier === 2;
  });

  const prettyPrintButton = await waitUntil(() => {
    return dbg.win.document.querySelector(".source-footer .prettyPrint.active");
  });

  dump("Click pretty-print button\n");
  const test = runTest("custom.jsdebugger.pretty-print.DAMP");
  prettyPrintButton.click();
  await waitForSource(dbg, formattedFileUrl);
  await waitForText(dbg, filePrettyChars);
  test.done();

  await addBreakpoint(dbg, 776, formattedFileUrl);

  const onPaused = waitForPaused(dbg);
  const reloadAndPauseInPrettyPrintedFileTest = runTest(
    "custom.jsdebugger.pretty-print.reload-and-pause.DAMP"
  );
  await reloadDebuggerAndLog("custom.pretty-print", toolbox, {
    sources: 1105,
    sourceURL: formattedFileUrl,
    text: filePrettyChars,
    threadsCount: EXPECTED.threadsCount,
  });
  await onPaused;

  // When reloading, the `togglePrettyPrint` action is called to pretty print the minified source.
  // This action is quite slow and finishes by ensuring that breakpoints are updated according to
  // the new pretty printed source.
  // We have to wait for this, otherwise breakpoints may be added after we remove all breakpoints just after.
  await waitForState(
    dbg,
    function (state) {
      const breakpoints = dbg.selectors.getBreakpointsAtLine(state, 776);
      const source = findSource(dbg, formattedFileUrl);
      // We have to ensure that the breakpoint is specific to the very last source object,
      // and not the one from the previous page load.
      return (
        breakpoints?.length > 0 && breakpoints[0].location.source == source
      );
    },
    "wait for pretty print breakpoint"
  );

  reloadAndPauseInPrettyPrintedFileTest.done();

  // The previous code waiting for state change isn't quite enough,
  // we need to spin the event loop once before clearing the breakpoints as
  // the processing of the new pretty printed source may still create late breakpoints
  // when it tries to update the breakpoint location on the pretty printed source.
  await new Promise(r => setTimeout(r, 0));

  await removeBreakpoints(dbg);
  await resume(dbg);

  // Clear the selection to avoid the source to be re-pretty printed on next load
  // Clear the selection before closing the tabs, otherwise closeTabs will reselect a random source.
  await dbg.actions.clearSelectedLocation();

  // Close tabs and especially the pretty printed one to stop pretty printing it.
  // Given that it is hard to find the non-pretty printed source via `findSource`
  // (because bundle and pretty print sources use almost the same URL except ':formatted' for the pretty printed one)
  // let's close all the tabs.
  const sources = dbg.selectors.getSourceList(dbg.getState());
  await dbg.actions.closeTabs(sources);

  await garbageCollect();
}

async function testBigBundle(dbg, tab) {
  const EXPECTED = {
    sources: 1149,
    file: "big-bundle/index.js",
    sourceURL: `${PAGES_BASE_URL}custom/debugger/app-build/static/js/big-bundle/index.js`,
    text: "import './minified.js';",
    threadsCount: 2,
  };
  const EXPECTED_FUNCTION = "window.hitBreakpointInBigBundle()";
  const STEP_TESTS = [
    {
      stepCount: 1,
      location: { line: 7, file: "big-bundle/index.js" },
      key: "stepInNewSource.big-bundle",
      stepType: "stepIn",
    },
    {
      stepCount: 2,
      location: { line: 10194, file: "big-bundle/step-in-test.js" },
      key: "stepIn.big-bundle",
      stepType: "stepIn",
    },
    {
      stepCount: 2,
      location: { line: 16, file: "big-bundle/step-over-test.js" },
      key: "stepOver.big-bundle",
      stepType: "stepOver",
    },
    {
      stepCount: 2,
      location: { line: 998, file: "big-bundle/step-out-test.js" },
      key: "stepOut.big-bundle",
      stepType: "stepOut",
    },
  ];

  await waitForSource(dbg, EXPECTED.sourceURL);
  await selectSource(dbg, EXPECTED.file);

  await stepDebuggerAndLog(dbg, tab, EXPECTED_FUNCTION, STEP_TESTS);

  const sources = dbg.selectors.getSourceList(dbg.getState());
  await dbg.actions.closeTabs(sources);

  await garbageCollect();
}
