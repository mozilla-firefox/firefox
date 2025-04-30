/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// This file is loaded in a `spawn` context sometimes which doesn't have,
// `Assert`, so we can't use its comparison functions.
/* eslint-disable mozilla/no-comparison-or-assignment-inside-ok */

/**
 * Helper methods to drive with the debugger during mochitests. This file can be safely
 * required from other panel test files.
 */

"use strict";

/* eslint-disable no-unused-vars */

// We can't use "import globals from head.js" because of bug 1395426.
// So workaround by manually importing the few symbols we are using from it.
// (Note that only ./mach eslint devtools/client fails while devtools/client/debugger passes)
/* global EXAMPLE_URL, ContentTask */

// Assume that shared-head is always imported before this file
/* import-globals-from ../../../shared/test/shared-head.js */

/**
 * Helper method to create a "dbg" context for other tools to use
 */
function createDebuggerContext(toolbox) {
  const panel = toolbox.getPanel("jsdebugger");
  const win = panel.panelWin;

  return {
    ...win.dbg,
    commands: toolbox.commands,
    toolbox,
    win,
    panel,
  };
}

var { Toolbox } = require("devtools/client/framework/toolbox");
const asyncStorage = require("devtools/shared/async-storage");

const {
  getSelectedLocation,
} = require("devtools/client/debugger/src/utils/selected-location");
const {
  createLocation,
} = require("devtools/client/debugger/src/utils/location");

const {
  resetSchemaVersion,
} = require("devtools/client/debugger/src/utils/prefs");

const {
  getUnicodeUrlPath,
} = require("resource://devtools/client/shared/unicode-url.js");

const {
  isGeneratedId,
} = require("devtools/client/shared/source-map-loader/index");

const DEBUGGER_L10N = new LocalizationHelper(
  "devtools/client/locales/debugger.properties"
);

const isCm6Enabled = Services.prefs.getBoolPref(
  "devtools.debugger.features.codemirror-next"
);

/**
 * Waits for `predicate()` to be true. `state` is the redux app state.
 *
 * @param {Object} dbg
 * @param {Function} predicate
 * @param {String} msg
 * @return {Promise}
 */
function waitForState(dbg, predicate, msg = "") {
  return new Promise(resolve => {
    info(`Waiting for state change: ${msg}`);
    let result = predicate(dbg.store.getState());
    if (result) {
      info(
        `--> The state was immediately correct (should rather do an immediate assertion?)`
      );
      resolve(result);
      return;
    }

    const unsubscribe = dbg.store.subscribe(
      () => {
        result = predicate(dbg.store.getState());
        if (result) {
          info(`Finished waiting for state change: ${msg}`);
          unsubscribe();
          resolve(result);
        }
      },
      // The `visibilityHandlerStore` wrapper may prevent the test helper from being
      // notified about store updates while the debugger is in background.
      { ignoreVisibility: true }
    );
  });
}

/**
 * Waits for sources to be loaded.
 *
 * @memberof mochitest/waits
 * @param {Object} dbg
 * @param {Array} sources
 * @return {Promise}
 * @static
 */
async function waitForSources(dbg, ...sources) {
  if (sources.length === 0) {
    return;
  }

  info(`Waiting on sources: ${sources.join(", ")}`);
  await Promise.all(
    sources.map(url => {
      if (!sourceExists(dbg, url)) {
        return waitForState(
          dbg,
          () => sourceExists(dbg, url),
          `source ${url} exists`
        );
      }
      return Promise.resolve();
    })
  );

  info(`Finished waiting on sources: ${sources.join(", ")}`);
}

/**
 * Waits for a source to be loaded.
 *
 * @memberof mochitest/waits
 * @param {Object} dbg
 * @param {String} source
 * @return {Promise}
 * @static
 */
function waitForSource(dbg, url) {
  return waitForState(
    dbg,
    () => findSource(dbg, url, { silent: true }),
    "source exists"
  );
}

async function waitForElement(dbg, name, ...args) {
  info(`Waiting for debugger element by name: ${name}`);
  await waitUntil(() => findElement(dbg, name, ...args));
  return findElement(dbg, name, ...args);
}

/**
 * Wait for a count of given elements to be rendered on screen.
 *
 * @param {DebuggerPanel} dbg
 * @param {String} name
 * @param {Integer} count: Number of elements to match. Defaults to 1.
 * @param {Boolean} countStrictlyEqual: When set to true, will wait until the exact number
 *                  of elements is displayed on screen. When undefined or false, will wait
 *                  until there's at least `${count}` elements on screen (e.g. if count
 *                  is 1, it will resolve if there are 2 elements rendered).
 */
async function waitForAllElements(
  dbg,
  name,
  count = 1,
  countStrictlyEqual = false
) {
  info(`Waiting for N=${count} debugger elements by name: ${name}`);
  await waitUntil(() => {
    const elsCount = findAllElements(dbg, name).length;
    return countStrictlyEqual ? elsCount === count : elsCount >= count;
  });
  return findAllElements(dbg, name);
}

async function waitForElementWithSelector(dbg, selector) {
  info(`Waiting for debugger element by selector: ${selector}`);
  await waitUntil(() => findElementWithSelector(dbg, selector));
  return findElementWithSelector(dbg, selector);
}

function waitForRequestsToSettle(dbg) {
  return dbg.commands.client.waitForRequestsToSettle();
}

function assertClass(el, className, exists = true) {
  if (exists) {
    ok(el.classList.contains(className), `${className} class exists`);
  } else {
    ok(!el.classList.contains(className), `${className} class does not exist`);
  }
}

function waitForSelectedLocation(dbg, line, column) {
  return waitForState(dbg, () => {
    const location = dbg.selectors.getSelectedLocation();
    return (
      location &&
      (line ? location.line == line : true) &&
      (column ? location.column == column : true)
    );
  });
}

/**
 * Wait for a given source to be selected and ready.
 *
 * @memberof mochitest/waits
 * @param {Object} dbg
 * @param {null|string|Source} sourceOrUrl Optional. Either a source URL (string) or a source object (typically fetched via `findSource`)
 * @return {Promise}
 * @static
 */
function waitForSelectedSource(dbg, sourceOrUrl) {
  const {
    getSelectedSourceTextContent,
    getSymbols,
    getBreakableLines,
    getSourceActorsForSource,
    getSourceActorBreakableLines,
    getFirstSourceActorForGeneratedSource,
    getSelectedFrame,
    getCurrentThread,
  } = dbg.selectors;

  return waitForState(
    dbg,
    () => {
      const location = dbg.selectors.getSelectedLocation() || {};
      const sourceTextContent = getSelectedSourceTextContent();
      if (!sourceTextContent) {
        return false;
      }

      if (sourceOrUrl) {
        // Second argument is either a source URL (string)
        // or a Source object.
        if (typeof sourceOrUrl == "string") {
          const url = location.source.url;
          if (typeof url != "string" || !url.includes(encodeURI(sourceOrUrl))) {
            return false;
          }
        } else if (location.source.id != sourceOrUrl.id) {
          return false;
        }
      }

      const selectedFrame = getSelectedFrame(getCurrentThread());
      // Wait for symbols/AST to be parsed only when CM5 is enabled
      const hasSymbols = !isCm6Enabled ? getSymbols(location) : true;
      // And this isn't a WASM source (which has no AST)
      if (
        // Only when we are paused on that specific source
        selectedFrame?.location.source.id == location.source.id &&
        !hasSymbols &&
        // And this isn't a WASM source (which has no AST)
        !isWasmBinarySource(location.source)
      ) {
        return false;
      }

      // Finaly wait for breakable lines to be set
      if (location.source.isHTML) {
        // For HTML sources we need to wait for each source actor to be processed.
        // getBreakableLines will return the aggregation without being able to know
        // if that's complete, with all the source actors.
        const sourceActors = getSourceActorsForSource(location.source.id);
        const allSourceActorsProcessed = sourceActors.every(
          sourceActor => !!getSourceActorBreakableLines(sourceActor.id)
        );
        return allSourceActorsProcessed;
      }

      if (!getBreakableLines(location.source.id)) {
        return false;
      }

      // Also ensure that CodeMirror updated its content
      return getEditorContent(dbg) !== DEBUGGER_L10N.getStr("loadingText");
    },
    "selected source"
  );
}

/**
 * The generated source of WASM source are WASM binary file,
 * which have many broken/disabled features in the debugger.
 *
 * They especially have a very special behavior in CodeMirror
 * where line labels aren't line number, but hex addresses.
 */
function isWasmBinarySource(source) {
  return source.isWasm && !source.isOriginal;
}

function getVisibleSelectedFrameLine(dbg) {
  const frame = dbg.selectors.getVisibleSelectedFrame();
  return frame?.location.line;
}

function getVisibleSelectedFrameColumn(dbg) {
  const frame = dbg.selectors.getVisibleSelectedFrame();
  return frame?.location.column;
}

/**
 * Assert that a given line is breakable or not.
 * Verify that CodeMirror gutter is grayed out via the empty line classname if not breakable.
 */
async function assertLineIsBreakable(dbg, file, line, shouldBeBreakable) {
  const el = await getNodeAtEditorGutterLine(dbg, line);
  const lineText = `${line}| ${el.innerText.substring(0, 50)}${
    el.innerText.length > 50 ? "…" : ""
  } — in ${file}`;
  // When a line is not breakable, the "empty-line" class is added
  // and the line is greyed out
  if (shouldBeBreakable) {
    ok(!el.classList.contains("empty-line"), `${lineText} should be breakable`);
  } else {
    ok(
      el.classList.contains("empty-line"),
      `${lineText} should NOT be breakable`
    );
  }
}

/**
 * Assert that the debugger is highlighting the correct location.
 *
 * @memberof mochitest/asserts
 * @param {Object} dbg
 * @param {String} source
 * @param {Number} line
 * @static
 */
function assertHighlightLocation(dbg, source, line) {
  source = findSource(dbg, source);

  // Check the selected source
  is(
    dbg.selectors.getSelectedSource().url,
    source.url,
    "source url is correct"
  );

  // Check the highlight line
  const lineEl = findElement(dbg, "highlightLine");
  ok(lineEl, "Line is highlighted");

  is(
    findAllElements(dbg, "highlightLine").length,
    1,
    "Only 1 line is highlighted"
  );

  ok(isVisibleInEditor(dbg, lineEl), "Highlighted line is visible");

  const lineInfo = getCMEditor(dbg).lineInfo(isCm6Enabled ? line : line - 1);
  ok(lineInfo.wrapClass.includes("highlight-line"), "Line is highlighted");
}

/**
 * Helper function for assertPausedAtSourceAndLine.
 *
 * Assert that CodeMirror reports to be paused at the given line/column.
 */
async function _assertDebugLine(dbg, line, column) {
  const source = dbg.selectors.getSelectedSource();
  // WASM lines are hex addresses which have to be mapped to decimal line number
  if (isWasmBinarySource(source)) {
    line = wasmOffsetToLine(dbg, source.id, line);
  }

  // Check the debug line
  // cm6 lines are 1-based, while cm5 are 0-based, to keep compatibility with
  // .lineInfo usage in other locations.
  const lineInfo = getCMEditor(dbg).lineInfo(isCm6Enabled ? line : line - 1);
  const sourceTextContent = dbg.selectors.getSelectedSourceTextContent();
  if (source && !sourceTextContent) {
    const url = source.url;
    ok(
      false,
      `Looks like the source ${url} is still loading. Try adding waitForLoadedSource in the test.`
    );
    return;
  }

  // Scroll the line into view to make sure the content
  // on the line is rendered and in the dom.
  await scrollEditorIntoView(dbg, line, 0);

  if (!lineInfo.wrapClass) {
    const pauseLine = getVisibleSelectedFrameLine(dbg);
    ok(false, `Expected pause line on line ${line}, it is on ${pauseLine}`);
    return;
  }

  // Consider pausing on error also as being paused
  ok(
    lineInfo?.wrapClass.includes("paused-line") ||
      lineInfo?.wrapClass.includes("new-debug-line-error"),
    `Line ${line} is not highlighted as paused`
  );

  const pausedLine =
    findElement(dbg, "pausedLine") || findElement(dbg, "debugErrorLine");

  is(
    findAllElements(dbg, "pausedLine").length +
      findAllElements(dbg, "debugErrorLine").length,
    1,
    "There is only one line"
  );

  ok(isVisibleInEditor(dbg, pausedLine), "debug line is visible");

  if (isCm6Enabled) {
    const editorLineEl = getCMEditor(dbg).getElementAtLine(line);
    const pauseLocationMarker = editorLineEl.querySelector(".paused-location");
    is(
      pauseLocationMarker.cmView.widget.line,
      line,
      "The paused caret is at the right line"
    );
    is(
      pauseLocationMarker.cmView.widget.column,
      column,
      "The paused caret is at the right column"
    );
  } else {
    const markedSpans = lineInfo.handle.markedSpans;
    if (markedSpans && markedSpans.length && !isWasmBinarySource(source)) {
      const hasExpectedDebugLine = markedSpans.some(
        span =>
          span.marker.className?.includes("debug-expression") &&
          // When a precise column is expected, ensure that we have at least
          // one "debug line" for the column we expect.
          // (See the React Component: DebugLine.setDebugLine)
          (!column || span.from == column)
      );
      ok(
        hasExpectedDebugLine,
        "Got the expected DebugLine. i.e. got the right marker in codemirror visualizing the breakpoint"
      );
    }
  }
  info(`Paused on line ${line}`);
}

/**
 * Make sure the debugger is paused at a certain source ID and line.
 *
 * @param {Object} dbg
 * @param {String} expectedSourceId
 * @param {Number} expectedLine
 * @param {Number} [expectedColumn]
 */
async function assertPausedAtSourceAndLine(
  dbg,
  expectedSourceId,
  expectedLine,
  expectedColumn
) {
  // Check that the debugger is paused.
  assertPaused(dbg);

  // Check that the paused location is correctly rendered.
  ok(isSelectedFrameSelected(dbg), "top frame's source is selected");

  // Check the pause location
  const pauseLine = getVisibleSelectedFrameLine(dbg);
  is(
    pauseLine,
    expectedLine,
    "Redux state for currently selected frame's line is correct"
  );

  const selectedSource = dbg.selectors.getSelectedSource();
  // WASM binary source is pausing at 0 column, whereas visible selected frame returns 1
  const pauseColumn = isWasmBinarySource(selectedSource)
    ? 0
    : getVisibleSelectedFrameColumn(dbg);
  if (expectedColumn) {
    // `pauseColumn` is 0-based, coming from internal state,
    // while `expectedColumn` is manually passed from test scripts and so is 1-based.
    is(
      pauseColumn + 1,
      expectedColumn,
      "Redux state for currently selected frame's column is correct"
    );
  }
  await _assertDebugLine(dbg, pauseLine, pauseColumn);

  ok(isVisibleInEditor(dbg, findElement(dbg, "gutters")), "gutter is visible");

  const frames = dbg.selectors.getCurrentThreadFrames();

  // WASM support is limited when we are on the generated binary source
  if (isWasmBinarySource(selectedSource)) {
    return;
  }

  ok(frames.length >= 1, "Got at least one frame");

  // Lets make sure we can assert both original and generated file locations when needed
  const { source, line, column } = isGeneratedId(expectedSourceId)
    ? frames[0].generatedLocation
    : frames[0].location;
  is(source.id, expectedSourceId, "Frame has correct source");
  is(
    line,
    expectedLine,
    `Frame paused at line ${line}, but expected line ${expectedLine}`
  );

  if (expectedColumn) {
    // `column` is 0-based, coming from internal state,
    // while `expectedColumn` is manually passed from test scripts and so is 1-based.
    is(
      column + 1,
      expectedColumn,
      `Frame paused at column ${
        column + 1
      }, but expected column ${expectedColumn}`
    );
  }
}

async function waitForThreadCount(dbg, count) {
  return waitForState(
    dbg,
    state => dbg.selectors.getThreads(state).length == count
  );
}

async function waitForLoadedScopes(dbg) {
  const scopes = await waitForElement(dbg, "scopes");
  // Since scopes auto-expand, we can assume they are loaded when there is a tree node
  // with the aria-level attribute equal to "2".
  info("Wait for loaded scopes - ie when a tree node has aria-level=2");
  await waitUntil(() => scopes.querySelector('.tree-node[aria-level="2"]'));
}

function waitForBreakpointCount(dbg, count) {
  return waitForState(dbg, () => dbg.selectors.getBreakpointCount() == count);
}

function waitForBreakpoint(dbg, url, line) {
  return waitForState(dbg, () => findBreakpoint(dbg, url, line));
}

function waitForBreakpointRemoved(dbg, url, line) {
  return waitForState(dbg, () => !findBreakpoint(dbg, url, line));
}

/**
 * Returns boolean for whether the debugger is paused.
 *
 * @param {Object} dbg
 */
function isPaused(dbg) {
  return dbg.selectors.getIsCurrentThreadPaused();
}

/**
 * Assert that the debugger is not currently paused.
 *
 * @param {Object} dbg
 * @param {String} msg
 *        Optional assertion message
 */
function assertNotPaused(dbg, msg = "client is not paused") {
  ok(!isPaused(dbg), msg);
}

/**
 * Assert that the debugger is currently paused.
 *
 * @param {Object} dbg
 */
function assertPaused(dbg, msg = "client is paused") {
  ok(isPaused(dbg), msg);
}

/**
 * Waits for the debugger to be fully paused.
 *
 * @param {Object} dbg
 * @param {String} url
 *        Optional URL of the script we should be pausing on.
 * @param {Object} options
 *         {Boolean} shouldWaitForLoadScopes
 *        When paused in original files with original variable mapping disabled, scopes are
 *        not going to exist, lets not wait for it. defaults to true
 */
async function waitForPaused(
  dbg,
  url,
  options = {
    shouldWaitForLoadedScopes: true,
    shouldWaitForInlinePreviews: true,
  }
) {
  info("Waiting for the debugger to pause");
  const { getSelectedScope, getCurrentThread, getCurrentThreadFrames } =
    dbg.selectors;

  await waitForState(
    dbg,
    () => isPaused(dbg) && !!getSelectedScope(),
    "paused"
  );

  await waitForState(dbg, getCurrentThreadFrames, "fetched frames");

  if (options.shouldWaitForLoadedScopes) {
    await waitForLoadedScopes(dbg);
  }

  // Note that this will wait for symbols (when CM5 is enabled),
  // which are fetched on pause
  await waitForSelectedSource(dbg, url);

  if (options.shouldWaitForInlinePreviews) {
    await waitForInlinePreviews(dbg);
  }
}

/**
 * Waits for the debugger to resume.
 *
 * @param {Objeect} dbg
 */
function waitForResumed(dbg) {
  info("Waiting for the debugger to resume");
  return waitForState(dbg, () => !dbg.selectors.getIsCurrentThreadPaused());
}

function waitForInlinePreviews(dbg) {
  return waitForState(dbg, () => dbg.selectors.getInlinePreviews());
}

function waitForCondition(dbg, condition) {
  return waitForState(dbg, () =>
    dbg.selectors
      .getBreakpointsList()
      .find(bp => bp.options.condition == condition)
  );
}

function waitForLog(dbg, logValue) {
  return waitForState(dbg, () =>
    dbg.selectors
      .getBreakpointsList()
      .find(bp => bp.options.logValue == logValue)
  );
}

async function waitForPausedThread(dbg, thread) {
  return waitForState(dbg, () => dbg.selectors.getIsPaused(thread));
}

function isSelectedFrameSelected(dbg) {
  const frame = dbg.selectors.getVisibleSelectedFrame();

  // Make sure the source text is completely loaded for the
  // source we are paused in.
  const source = dbg.selectors.getSelectedSource();
  const sourceTextContent = dbg.selectors.getSelectedSourceTextContent();

  if (!source || !sourceTextContent) {
    return false;
  }

  return source.id == frame.location.source.id;
}

/**
 * Checks to see if the frame is selected and the displayed title is correct.
 *
 * @param {Object} dbg
 * @param {DOM Node} frameElement
 * @param {String} expectedTitle
 */
function assertFrameIsSelected(dbg, frameElement, expectedTitle) {
  const selectedFrame = dbg.selectors.getSelectedFrame();
  ok(frameElement.classList.contains("selected"), "The frame is selected");
  is(
    frameElement.querySelector(".title").innerText,
    expectedTitle,
    "The selected frame element has the expected title"
  );
  // For `<anonymous>` frames, there is likely no displayName
  is(
    selectedFrame.displayName,
    expectedTitle == "<anonymous>" ? undefined : expectedTitle,
    "The selected frame has the correct display title"
  );
}

/**
 * Checks to see if the frame is  not selected.
 *
 * @param {Object} dbg
 * @param {DOM Node} frameElement
 * @param {String} expectedTitle
 */
function assertFrameIsNotSelected(dbg, frameElement, expectedTitle) {
  const selectedFrame = dbg.selectors.getSelectedFrame();
  ok(!frameElement.classList.contains("selected"), "The frame is selected");
  is(
    frameElement.querySelector(".title").innerText,
    expectedTitle,
    "The selected frame element has the expected title"
  );
}

/**
 *  Clear all the debugger related preferences.
 */
async function clearDebuggerPreferences(prefs = []) {
  resetSchemaVersion();
  asyncStorage.clear();
  Services.prefs.clearUserPref("devtools.debugger.alphabetize-outline");
  Services.prefs.clearUserPref("devtools.debugger.pause-on-exceptions");
  Services.prefs.clearUserPref("devtools.debugger.pause-on-caught-exceptions");
  Services.prefs.clearUserPref("devtools.debugger.ignore-caught-exceptions");
  Services.prefs.clearUserPref("devtools.debugger.pending-selected-location");
  Services.prefs.clearUserPref("devtools.debugger.expressions");
  Services.prefs.clearUserPref("devtools.debugger.breakpoints-visible");
  Services.prefs.clearUserPref("devtools.debugger.call-stack-visible");
  Services.prefs.clearUserPref("devtools.debugger.scopes-visible");
  Services.prefs.clearUserPref("devtools.debugger.skip-pausing");

  for (const pref of prefs) {
    await pushPref(...pref);
  }
}

/**
 * Intilializes the debugger.
 *
 * @memberof mochitest
 * @param {String} url
 * @return {Promise} dbg
 * @static
 */

async function initDebugger(url, ...sources) {
  // We depend on EXAMPLE_URLs origin to do cross origin/process iframes via
  // EXAMPLE_REMOTE_URL. If the top level document origin changes,
  // we may break this. So be careful if you want to change EXAMPLE_URL.
  return initDebuggerWithAbsoluteURL(EXAMPLE_URL + url, ...sources);
}

async function initDebuggerWithAbsoluteURL(url, ...sources) {
  await clearDebuggerPreferences();
  const toolbox = await openNewTabAndToolbox(url, "jsdebugger");
  const dbg = createDebuggerContext(toolbox);

  await waitForSources(dbg, ...sources);
  return dbg;
}

async function initPane(url, pane, prefs) {
  await clearDebuggerPreferences(prefs);
  return openNewTabAndToolbox(EXAMPLE_URL + url, pane);
}

/**
 * Returns a source that matches a given filename, or a URL.
 * This also accept a source as input argument, in such case it just returns it.
 *
 * @param {Object} dbg
 * @param {String} filenameOrUrlOrSource
 *        The typical case will be to pass only a filename,
 *        but you may also pass a full URL to match sources without filesnames like data: URL
 *        or pass the source itself, which is just returned.
 * @param {Object} options
 * @param {Boolean} options.silent
 *        If true, won't throw if the source is missing.
 * @return {Object} source
 */
function findSource(
  dbg,
  filenameOrUrlOrSource,
  { silent } = { silent: false }
) {
  if (typeof filenameOrUrlOrSource !== "string") {
    // Support passing in a source object itself all APIs that use this
    // function support both styles
    return filenameOrUrlOrSource;
  }

  const sources = dbg.selectors.getSourceList();
  const source = sources.find(s => {
    // Sources don't have a file name attribute, we need to compute it here:
    const sourceFileName = s.url
      ? getUnicodeUrlPath(s.url.substring(s.url.lastIndexOf("/") + 1))
      : "";

    // The input argument may either be only the filename, or the complete URL
    // This helps match sources whose URL doesn't contain a filename, like data: URLs
    return (
      sourceFileName == filenameOrUrlOrSource || s.url == filenameOrUrlOrSource
    );
  });

  if (!source) {
    if (silent) {
      return false;
    }

    throw new Error(`Unable to find source: ${filenameOrUrlOrSource}`);
  }

  return source;
}

function findSourceContent(dbg, url, opts) {
  const source = findSource(dbg, url, opts);

  if (!source) {
    return null;
  }
  const content = dbg.selectors.getSettledSourceTextContent(
    createLocation({
      source,
    })
  );

  if (!content) {
    return null;
  }

  if (content.state !== "fulfilled") {
    throw new Error(`Expected loaded source, got${content.value}`);
  }

  return content.value;
}

function sourceExists(dbg, url) {
  return !!findSource(dbg, url, { silent: true });
}

function waitForLoadedSource(dbg, url) {
  return waitForState(
    dbg,
    () => {
      const source = findSource(dbg, url, { silent: true });
      return (
        source &&
        dbg.selectors.getSettledSourceTextContent(
          createLocation({
            source,
          })
        )
      );
    },
    "loaded source"
  );
}

/*
 * Selects the source node for a specific source
 * from the source tree.
 *
 * @param {Object} dbg
 * @param {String} filename - The filename for the specific source
 */
async function selectSourceFromSourceTree(dbg, fileName) {
  info(`Selecting '${fileName}' source from source tree`);
  // Ensure that the source is visible in the tree before trying to click on it
  const elt = await waitForSourceInSourceTree(dbg, fileName);
  elt.scrollIntoView();
  clickDOMElement(dbg, elt);
  await waitForSelectedSource(dbg, fileName);
  await waitFor(
    () => getEditorContent(dbg) !== `Loading…`,
    "Wait for source to completely load"
  );
}

/**
 * Similar to selectSourceFromSourceTree, but with a precise location
 * in the source tree.
 */
async function selectSourceFromSourceTreeWithIndex(
  dbg,
  fileName,
  sourcePosition,
  message
) {
  info(message);
  await clickElement(dbg, "sourceNode", sourcePosition);
  await waitForSelectedSource(dbg, fileName);
  await waitFor(
    () => getEditorContent(dbg) !== `Loading…`,
    "Wait for source to completely load"
  );
}

/*
 * Trigger a context menu in the debugger source tree
 *
 * @param {Object} dbg
 * @param {Obejct} sourceTreeNode - The node in the source tree which the context menu
 *                                  item needs to be triggered on.
 * @param {String} contextMenuItem - The id for the context menu item to be selected
 */
async function triggerSourceTreeContextMenu(
  dbg,
  sourceTreeNode,
  contextMenuItem
) {
  const onContextMenu = waitForContextMenu(dbg);
  rightClickEl(dbg, sourceTreeNode);
  const menupopup = await onContextMenu;
  const onHidden = new Promise(resolve => {
    menupopup.addEventListener("popuphidden", resolve, { once: true });
  });
  selectDebuggerContextMenuItem(dbg, contextMenuItem);
  await onHidden;
}

/**
 * Selects the source.
 *
 * @memberof mochitest/actions
 * @param {Object} dbg
 * @param {String} url
 * @param {Number} line
 * @param {Number} column
 * @return {Promise}
 * @static
 */
async function selectSource(dbg, url, line, column) {
  const source = findSource(dbg, url);

  await dbg.actions.selectLocation(createLocation({ source, line, column }), {
    keepContext: false,
  });
  return waitForSelectedSource(dbg, source);
}

async function closeTab(dbg, url) {
  await dbg.actions.closeTab(findSource(dbg, url));
}

function countTabs(dbg) {
  return findElement(dbg, "sourceTabs").children.length;
}

/**
 * Steps over.
 *
 * @memberof mochitest/actions
 * @param {Object} dbg
 * @param {Object} pauseOptions
 * @return {Promise}
 * @static
 */
async function stepOver(dbg, pauseOptions) {
  const pauseLine = getVisibleSelectedFrameLine(dbg);
  info(`Stepping over from ${pauseLine}`);
  await dbg.actions.stepOver();
  return waitForPaused(dbg, null, pauseOptions);
}

/**
 * Steps in.
 *
 * @memberof mochitest/actions
 * @param {Object} dbg
 * @return {Promise}
 * @static
 */
async function stepIn(dbg) {
  const pauseLine = getVisibleSelectedFrameLine(dbg);
  info(`Stepping in from ${pauseLine}`);
  await dbg.actions.stepIn();
  return waitForPaused(dbg);
}

/**
 * Steps out.
 *
 * @memberof mochitest/actions
 * @param {Object} dbg
 * @return {Promise}
 * @static
 */
async function stepOut(dbg) {
  const pauseLine = getVisibleSelectedFrameLine(dbg);
  info(`Stepping out from ${pauseLine}`);
  await dbg.actions.stepOut();
  return waitForPaused(dbg);
}

/**
 * Resumes.
 *
 * @memberof mochitest/actions
 * @param {Object} dbg
 * @return {Promise}
 * @static
 */
async function resume(dbg) {
  const pauseLine = getVisibleSelectedFrameLine(dbg);
  info(`Resuming from ${pauseLine}`);
  const onResumed = waitForResumed(dbg);
  await dbg.actions.resume();
  return onResumed;
}

function deleteExpression(dbg, input) {
  info(`Delete expression "${input}"`);
  return dbg.actions.deleteExpression({ input });
}

/**
 * Reloads the debuggee.
 *
 * @memberof mochitest/actions
 * @param {Object} dbg
 * @param {Array} sources
 * @return {Promise}
 * @static
 */
async function reload(dbg, ...sources) {
  await reloadBrowser();
  return waitForSources(dbg, ...sources);
}

// Only use this method when the page is paused by the debugger
// during page load and we navigate away without resuming.
//
// In this particular scenario, the page will never be "loaded".
// i.e. emit DOCUMENT_EVENT's dom-complete
// And consequently, debugger panel won't emit "reloaded" event.
async function reloadWhenPausedBeforePageLoaded(dbg, ...sources) {
  // But we can at least listen for the next DOCUMENT_EVENT's dom-loading,
  // which should be fired even if the page is pause the earliest.
  const { resourceCommand } = dbg.commands;
  const { onResource: onTopLevelDomLoading } =
    await resourceCommand.waitForNextResource(
      resourceCommand.TYPES.DOCUMENT_EVENT,
      {
        ignoreExistingResources: true,
        predicate: resource =>
          resource.targetFront.isTopLevel && resource.name === "dom-loading",
      }
    );

  gBrowser.reloadTab(gBrowser.selectedTab);

  info("Wait for DOCUMENT_EVENT dom-loading after reload");
  await onTopLevelDomLoading;
  return waitForSources(dbg, ...sources);
}

/**
 * Navigates the debuggee to another url.
 *
 * @memberof mochitest/actions
 * @param {Object} dbg
 * @param {String} url
 * @param {Array} sources
 * @return {Promise}
 * @static
 */
async function navigate(dbg, url, ...sources) {
  return navigateToAbsoluteURL(dbg, EXAMPLE_URL + url, ...sources);
}

/**
 * Navigates the debuggee to another absolute url.
 *
 * @memberof mochitest/actions
 * @param {Object} dbg
 * @param {String} url
 * @param {Array} sources
 * @return {Promise}
 * @static
 */
async function navigateToAbsoluteURL(dbg, url, ...sources) {
  await navigateTo(url);
  return waitForSources(dbg, ...sources);
}

function getFirstBreakpointColumn(dbg, source, line) {
  const position = dbg.selectors.getFirstBreakpointPosition(
    createLocation({
      line,
      source,
    })
  );

  return getSelectedLocation(position, source).column;
}

function isMatchingLocation(location1, location2) {
  return (
    location1?.source.id == location2?.source.id &&
    location1?.line == location2?.line &&
    location1?.column == location2?.column
  );
}

function getBreakpointForLocation(dbg, location) {
  if (!location) {
    return undefined;
  }

  const isGeneratedSource = isGeneratedId(location.source.id);
  return dbg.selectors.getBreakpointsList().find(bp => {
    const loc = isGeneratedSource ? bp.generatedLocation : bp.location;
    return isMatchingLocation(loc, location);
  });
}

/**
 * Adds a breakpoint to a source at line/col.
 *
 * @memberof mochitest/actions
 * @param {Object} dbg
 * @param {String} source
 * @param {Number} line
 * @param {Number} col
 * @return {Promise}
 * @static
 */
async function addBreakpoint(dbg, source, line, column, options) {
  source = findSource(dbg, source);
  const bpCount = dbg.selectors.getBreakpointCount();
  const onBreakpoint = waitForDispatch(dbg.store, "SET_BREAKPOINT");
  await dbg.actions.addBreakpoint(
    // column is 0-based internally, but tests are using 1-based.
    createLocation({ source, line, column: column - 1 }),
    options
  );
  await onBreakpoint;
  is(
    dbg.selectors.getBreakpointCount(),
    bpCount + 1,
    "a new breakpoint was created"
  );
}

// use shortcut to open conditional panel.
function setConditionalBreakpointWithKeyboardShortcut(dbg, condition) {
  pressKey(dbg, "toggleCondPanel");
  return typeInPanel(dbg, condition);
}

/**
 * Similar to `addBreakpoint`, but uses the UI instead or calling
 * the actions directly. This only support breakpoint on lines,
 * not on a specific column.
 */
async function addBreakpointViaGutter(dbg, line) {
  info(`Add breakpoint via the editor on line ${line}`);
  await clickGutter(dbg, line);
  return waitForDispatch(dbg.store, "SET_BREAKPOINT");
}

async function removeBreakpointViaGutter(dbg, line) {
  const onRemoved = waitForDispatch(dbg.store, "REMOVE_BREAKPOINT");
  await clickGutter(dbg, line);
  await onRemoved;
}

function disableBreakpoint(dbg, source, line, column) {
  if (column === 0) {
    throw new Error("disableBreakpoint expect a 1-based column argument");
  }
  // `internalColumn` is 0-based internally, while `column` manually defined in test scripts is 1-based.
  const internalColumn = column
    ? column - 1
    : getFirstBreakpointColumn(dbg, source, line);
  const location = createLocation({
    source,
    line,
    column: internalColumn,
  });
  const bp = getBreakpointForLocation(dbg, location);
  return dbg.actions.disableBreakpoint(bp);
}

function findBreakpoint(dbg, url, line) {
  const source = findSource(dbg, url);
  return dbg.selectors.getBreakpointsForSource(source, line)[0];
}

// helper for finding column breakpoints.
function findColumnBreakpoint(dbg, url, line, column) {
  const source = findSource(dbg, url);
  const lineBreakpoints = dbg.selectors.getBreakpointsForSource(source, line);

  return lineBreakpoints.find(bp => {
    return source.isOriginal
      ? bp.location.column === column
      : bp.generatedLocation.column === column;
  });
}

async function loadAndAddBreakpoint(dbg, filename, line, column) {
  const {
    selectors: { getBreakpoint, getBreakpointCount, getBreakpointsMap },
  } = dbg;

  await waitForSources(dbg, filename);

  ok(true, "Original sources exist");
  const source = findSource(dbg, filename);

  await selectSource(dbg, source);

  // Test that breakpoint is not off by a line.
  await addBreakpoint(dbg, source, line, column);

  is(getBreakpointCount(), 1, "One breakpoint exists");
  // column is 0-based internally, but tests are using 1-based.
  if (!getBreakpoint(createLocation({ source, line, column: column - 1 }))) {
    const breakpoints = getBreakpointsMap();
    const id = Object.keys(breakpoints).pop();
    const loc = breakpoints[id].location;
    ok(
      false,
      `Breakpoint has correct line ${line}, column ${column}, but was line ${
        loc.line
      } column ${loc.column + 1}`
    );
  }

  return source;
}

async function invokeWithBreakpoint(
  dbg,
  fnName,
  filename,
  { line, column },
  handler,
  pauseOptions
) {
  const source = await loadAndAddBreakpoint(dbg, filename, line, column);

  const invokeResult = invokeInTab(fnName);

  const invokeFailed = await Promise.race([
    waitForPaused(dbg, null, pauseOptions),
    invokeResult.then(
      () => new Promise(() => {}),
      () => true
    ),
  ]);

  if (invokeFailed) {
    await invokeResult;
    return;
  }

  await assertPausedAtSourceAndLine(
    dbg,
    findSource(dbg, filename).id,
    line,
    column
  );

  await removeBreakpoint(dbg, source.id, line, column);

  is(dbg.selectors.getBreakpointCount(), 0, "Breakpoint reverted");

  await handler(source);

  await resume(dbg);

  // eslint-disable-next-line max-len
  // If the invoke errored later somehow, capture here so the error is reported nicely.
  await invokeResult;
}

function prettyPrint(dbg) {
  const source = dbg.selectors.getSelectedSource();
  return dbg.actions.prettyPrintAndSelectSource(source);
}

async function expandAllScopes(dbg) {
  const scopes = await waitForElement(dbg, "scopes");
  const scopeElements = scopes.querySelectorAll(
    '.tree-node[aria-level="1"][data-expandable="true"]:not([aria-expanded="true"])'
  );
  const indices = Array.from(scopeElements, el => {
    return Array.prototype.indexOf.call(el.parentNode.childNodes, el);
  }).reverse();

  for (const index of indices) {
    await toggleScopeNode(dbg, index + 1);
  }
}

async function assertScopes(dbg, items) {
  await expandAllScopes(dbg);

  for (const [i, val] of items.entries()) {
    if (Array.isArray(val)) {
      is(getScopeNodeLabel(dbg, i + 1), val[0]);
      is(
        getScopeNodeValue(dbg, i + 1),
        val[1],
        `"${val[0]}" has the expected "${val[1]}" value`
      );
    } else {
      is(getScopeNodeLabel(dbg, i + 1), val);
    }
  }

  is(getScopeNodeLabel(dbg, items.length + 1), "Window");
}

function findSourceTreeThreadByName(dbg, name) {
  return [...findAllElements(dbg, "sourceTreeThreads")].find(el => {
    return el.textContent.includes(name);
  });
}

function findSourceTreeGroupByName(dbg, name) {
  return [...findAllElements(dbg, "sourceTreeGroups")].find(el => {
    return el.textContent.includes(name);
  });
}

function findSourceNodeWithText(dbg, text) {
  return [...findAllElements(dbg, "sourceNodes")].find(el => {
    return el.textContent.includes(text);
  });
}

/**
 * Assert the icon type used in the SourceTree for a given source
 *
 * @param {Object} dbg
 * @param {String} sourceName
 *        Name of the source displayed in the source tree
 * @param {String} icon
 *        Expected icon CSS classname
 */
function assertSourceIcon(dbg, sourceName, icon) {
  const sourceItem = findSourceNodeWithText(dbg, sourceName);
  ok(sourceItem, `Found the source item for ${sourceName}`);
  is(
    sourceItem.querySelector(".source-icon").className,
    `img source-icon ${icon}`,
    `The icon for ${sourceName} is correct`
  );
}

async function expandSourceTree(dbg) {
  // Click on expand all context menu for all top level "expandable items".
  // If there is no project root, it will be thread items.
  // But when there is a project root, it can be directory or group items.
  // Select only expandable in order to ignore source items.
  for (const rootNode of dbg.win.document.querySelectorAll(
    ".sources-list > .tree > .tree-node[data-expandable=true]"
  )) {
    await expandAllSourceNodes(dbg, rootNode);
  }
}

async function expandAllSourceNodes(dbg, treeNode) {
  return triggerSourceTreeContextMenu(dbg, treeNode, "#node-menu-expand-all");
}

/**
 * Removes a breakpoint from a source at line/col.
 *
 * @memberof mochitest/actions
 * @param {Object} dbg
 * @param {String} source
 * @param {Number} line
 * @param {Number} col
 * @return {Promise}
 * @static
 */
function removeBreakpoint(dbg, sourceId, line, column) {
  const source = dbg.selectors.getSource(sourceId);
  // column is 0-based internally, but tests are using 1-based.
  column = column ? column - 1 : getFirstBreakpointColumn(dbg, source, line);
  const location = createLocation({
    source,
    line,
    column,
  });
  const bp = getBreakpointForLocation(dbg, location);
  return dbg.actions.removeBreakpoint(bp);
}

/**
 * Toggles the Pause on exceptions feature in the debugger.
 *
 * @memberof mochitest/actions
 * @param {Object} dbg
 * @param {Boolean} pauseOnExceptions
 * @param {Boolean} pauseOnCaughtExceptions
 * @return {Promise}
 * @static
 */
async function togglePauseOnExceptions(
  dbg,
  pauseOnExceptions,
  pauseOnCaughtExceptions
) {
  return dbg.actions.pauseOnExceptions(
    pauseOnExceptions,
    pauseOnCaughtExceptions
  );
}

// Helpers

/**
 * Invokes a global function in the debuggee tab.
 *
 * @memberof mochitest/helpers
 * @param {String} fnc The name of a global function on the content window to
 *                     call. This is applied to structured clones of the
 *                     remaining arguments to invokeInTab.
 * @param {Any} ...args Remaining args to serialize and pass to fnc.
 * @return {Promise}
 * @static
 */
function invokeInTab(fnc, ...args) {
  info(`Invoking in tab: ${fnc}(${args.map(uneval).join(",")})`);
  return ContentTask.spawn(gBrowser.selectedBrowser, { fnc, args }, options =>
    content.wrappedJSObject[options.fnc](...options.args)
  );
}

function clickElementInTab(selector) {
  info(`click element ${selector} in tab`);

  return SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [selector],
    function (_selector) {
      const element = content.document.querySelector(_selector);
      // Run the click in another event loop in order to immediately resolve spawn's promise.
      // Otherwise if we pause on click and navigate, the JSWindowActor used by spawn will
      // be destroyed while its query is still pending. And this would reject the promise.
      content.setTimeout(() => {
        element.click();
      });
    }
  );
}

const isLinux = Services.appinfo.OS === "Linux";
const isMac = Services.appinfo.OS === "Darwin";
const cmdOrCtrl = isMac ? { metaKey: true } : { ctrlKey: true };
const shiftOrAlt = isMac
  ? { accelKey: true, shiftKey: true }
  : { accelKey: true, altKey: true };

const cmdShift = isMac
  ? { accelKey: true, shiftKey: true, metaKey: true }
  : { accelKey: true, shiftKey: true, ctrlKey: true };

// On Mac, going to beginning/end only works with meta+left/right.  On
// Windows, it only works with home/end.  On Linux, apparently, either
// ctrl+left/right or home/end work.
const endKey = isMac
  ? { code: "VK_RIGHT", modifiers: cmdOrCtrl }
  : { code: "VK_END" };
const startKey = isMac
  ? { code: "VK_LEFT", modifiers: cmdOrCtrl }
  : { code: "VK_HOME" };

const keyMappings = {
  close: { code: "w", modifiers: cmdOrCtrl },
  commandKeyDown: { code: "VK_META", modifiers: { type: "keydown" } },
  commandKeyUp: { code: "VK_META", modifiers: { type: "keyup" } },
  debugger: { code: "s", modifiers: shiftOrAlt },
  // test conditional panel shortcut
  toggleCondPanel: { code: "b", modifiers: cmdShift },
  toggleLogPanel: { code: "y", modifiers: cmdShift },
  toggleBreakpoint: { code: "b", modifiers: cmdOrCtrl },
  inspector: { code: "c", modifiers: shiftOrAlt },
  quickOpen: { code: "p", modifiers: cmdOrCtrl },
  quickOpenFunc: { code: "o", modifiers: cmdShift },
  quickOpenLine: { code: ":", modifiers: cmdOrCtrl },
  fileSearch: { code: "f", modifiers: cmdOrCtrl },
  projectSearch: { code: "f", modifiers: cmdShift },
  fileSearchNext: { code: "g", modifiers: { metaKey: true } },
  fileSearchPrev: { code: "g", modifiers: cmdShift },
  goToLine: { code: "g", modifiers: { ctrlKey: true } },
  sourceeditorGoToLine: { code: "j", modifiers: cmdOrCtrl },
  Enter: { code: "VK_RETURN" },
  ShiftEnter: { code: "VK_RETURN", modifiers: { shiftKey: true } },
  AltEnter: {
    code: "VK_RETURN",
    modifiers: { altKey: true },
  },
  Space: { code: "VK_SPACE" },
  Up: { code: "VK_UP" },
  Down: { code: "VK_DOWN" },
  Right: { code: "VK_RIGHT" },
  Left: { code: "VK_LEFT" },
  End: endKey,
  Start: startKey,
  Tab: { code: "VK_TAB" },
  ShiftTab: { code: "VK_TAB", modifiers: { shiftKey: true } },
  Escape: { code: "VK_ESCAPE" },
  Delete: { code: "VK_DELETE" },
  pauseKey: { code: "VK_F8" },
  resumeKey: { code: "VK_F8" },
  stepOverKey: { code: "VK_F10" },
  stepInKey: { code: "VK_F11" },
  stepOutKey: {
    code: "VK_F11",
    modifiers: { shiftKey: true },
  },
  Backspace: { code: "VK_BACK_SPACE" },
};

/**
 * Simulates a key press in the debugger window.
 *
 * @memberof mochitest/helpers
 * @param {Object} dbg
 * @param {String} keyName
 * @return {Promise}
 * @static
 */
function pressKey(dbg, keyName) {
  const keyEvent = keyMappings[keyName];
  const { code, modifiers } = keyEvent;
  info(`The ${keyName} key is pressed`);
  return EventUtils.synthesizeKey(code, modifiers || {}, dbg.win);
}

function type(dbg, string) {
  string.split("").forEach(char => EventUtils.synthesizeKey(char, {}, dbg.win));
}

/*
 * Checks to see if the inner element is visible inside the editor.
 *
 * @memberof mochitest/helpers
 * @param {Object} dbg
 * @param {HTMLElement} inner element
 * @return {boolean}
 * @static
 */

function isVisibleInEditor(dbg, element) {
  return isVisible(findElement(dbg, "codeMirror"), element);
}

/*
 * Checks to see if the inner element is visible inside the
 * outer element.
 *
 * Note, the inner element does not need to be entirely visible,
 * it is possible for it to be somewhat clipped by the outer element's
 * bounding element or for it to span the entire length, starting before the
 * outer element and ending after.
 *
 * @memberof mochitest/helpers
 * @param {HTMLElement} outer element
 * @param {HTMLElement} inner element
 * @return {boolean}
 * @static
 */
function isVisible(outerEl, innerEl) {
  if (!innerEl || !outerEl) {
    return false;
  }

  const innerRect = innerEl.getBoundingClientRect();
  const outerRect = outerEl.getBoundingClientRect();

  const verticallyVisible =
    innerRect.top >= outerRect.top ||
    innerRect.bottom <= outerRect.bottom ||
    (innerRect.top < outerRect.top && innerRect.bottom > outerRect.bottom);

  const horizontallyVisible =
    innerRect.left >= outerRect.left ||
    innerRect.right <= outerRect.right ||
    (innerRect.left < outerRect.left && innerRect.right > outerRect.right);

  const visible = verticallyVisible && horizontallyVisible;
  return visible;
}

/**
 * Get the element in the editor gutter at the specified line
 * @param {Object} dbg
 * @param {Number} line
 * @returns
 */
async function getEditorLineGutter(dbg, line) {
  let el = await codeMirrorGutterElement(dbg, line);
  while (el && !el.matches(".CodeMirror-code > div")) {
    el = el.parentElement;
  }

  return el;
}

// Handles virtualization scenarios
async function scrollAndGetEditorLineGutterElement(dbg, line) {
  const editor = getCMEditor(dbg);
  await scrollEditorIntoView(dbg, line, 0);
  const selectedSource = dbg.selectors.getSelectedSource();
  // For WASM sources get the hexadecimal line number displayed in the gutter
  if (editor.isWasm && !selectedSource.isOriginal) {
    const wasmLineFormatter = editor.getWasmLineNumberFormatter();
    line = wasmLineFormatter(line);
  }

  const els = findAllElementsWithSelector(
    dbg,
    isCm6Enabled
      ? ".cm-gutter.cm-lineNumbers .cm-gutterElement"
      : ".CodeMirror-code .CodeMirror-linenumber"
  );
  return [...els].find(el => el.innerText == line);
}

/**
 * Gets node at a specific line in the editor
 * @param {*} dbg
 * @param {Number} line
 * @returns {Element} DOM Element
 */
async function getNodeAtEditorLine(dbg, line) {
  if (isCm6Enabled) {
    // To handle virtualized lines accurately, lets use the
    // cm6 utility here.
    await scrollEditorIntoView(dbg, line, 0);
    return getCMEditor(dbg).getElementAtLine(line);
  }
  return getEditorLineGutter(dbg, line);
}

/**
 * Gets node at a specific line in the gutter
 * @param {*} dbg
 * @param {Number} line
 * @returns {Element} DOM Element
 */
async function getNodeAtEditorGutterLine(dbg, line) {
  if (isCm6Enabled) {
    return scrollAndGetEditorLineGutterElement(dbg, line);
  }
  // Note: In CM5 both the line gutter elements and the
  // line content elements are within the editor line.
  return getEditorLineGutter(dbg, line);
}

async function getConditionalPanelAtLine(dbg, line) {
  info(`Get conditional panel at line ${line}`);
  let el = await getNodeAtEditorLine(dbg, line);
  if (isCm6Enabled) {
    // In CM6 the conditional panel for a specific line
    // is injected in a sibling node just after.
    el = el.nextSibling;
  }
  return el.querySelector(".conditional-breakpoint-panel");
}

async function waitForConditionalPanelFocus(dbg) {
  if (isCm6Enabled) {
    return waitFor(
      () =>
        dbg.win.document.activeElement.classList.contains("cm-content") &&
        dbg.win.document.activeElement.closest(".conditional-breakpoint-panel")
    );
  }
  return waitFor(() => dbg.win.document.activeElement.tagName === "TEXTAREA");
}

/**
 * Opens the debugger editor context menu in either codemirror or the
 * the debugger gutter.
 * @param {Object} dbg
 * @param {String} elementName
 *                  The element to select
 * @param {Number} line
 *                  The line to open the context menu on.
 */
async function openContextMenuInDebugger(dbg, elementName, line) {
  const waitForOpen = waitForContextMenu(dbg);
  info(`Open ${elementName} context menu on line ${line || ""}`);
  rightClickElement(dbg, elementName, line);
  return waitForOpen;
}

/**
 * Select a range of lines in the editor and open the contextmenu
 * @param {Object} dbg
 * @param {Object} lines
 * @param {String} elementName
 * @returns
 */
async function selectEditorLinesAndOpenContextMenu(
  dbg,
  lines,
  elementName = "line"
) {
  const { startLine, endLine } = lines;
  setSelection(dbg, startLine, endLine ?? startLine);
  return openContextMenuInDebugger(dbg, elementName, startLine);
}

/**
 * Asserts that the styling for ignored lines are applied
 * @param {Object} dbg
 * @param {Object} options
 *                 lines {null | Number[]} [lines] Line(s) to assert.
 *                   - If null is passed, the assertion is on all the blackboxed lines
 *                   - If an array of one item (start line) is passed, the assertion is on the specified line
 *                   - If an array (start and end lines) is passed, the assertion is on the multiple lines seelected
 *                 hasBlackboxedLinesClass
 *                   If `true` assert that style exist, else assert that style does not exist
 */
async function assertIgnoredStyleInSourceLines(
  dbg,
  { lines, hasBlackboxedLinesClass }
) {
  if (lines) {
    let currentLine = lines[0];
    do {
      const element = await getNodeAtEditorLine(dbg, currentLine);
      const hasStyle = element.classList.contains("blackboxed-line");
      is(
        hasStyle,
        hasBlackboxedLinesClass,
        `Line ${currentLine} ${
          hasBlackboxedLinesClass ? "does not have" : "has"
        } ignored styling`
      );
      currentLine = currentLine + 1;
    } while (currentLine <= lines[1]);
  } else {
    const codeLines = findAllElements(dbg, "codeLines");
    const blackboxedLines = findAllElements(dbg, "blackboxedLines");
    is(
      hasBlackboxedLinesClass ? codeLines.length : 0,
      blackboxedLines.length,
      `${blackboxedLines.length} of ${codeLines.length} lines are blackboxed`
    );
  }
}

/**
 * Assert the text content on the line matches what is
 * expected.
 *
 * @param {Object} dbg
 * @param {Number} line
 * @param {String} expectedTextContent
 */
function assertTextContentOnLine(dbg, line, expectedTextContent) {
  const lineInfo = getCMEditor(dbg).lineInfo(isCm6Enabled ? line : line - 1);
  const textContent = lineInfo.text.trim();
  is(textContent, expectedTextContent, `Expected text content on line ${line}`);
}

/*
 * Assert that no breakpoint is set on a given line of
 * the currently selected source in the editor.
 *
 * @memberof mochitest/helpers
 * @param {Object} dbg
 * @param {Number} line Line where to check for a breakpoint in the editor
 * @static
 */
async function assertNoBreakpoint(dbg, line) {
  const el = await getNodeAtEditorGutterLine(dbg, line);

  const exists = el.classList.contains(
    isCm6Enabled ? "cm6-gutter-breakpoint" : "new-breakpioint"
  );
  ok(!exists, `Breakpoint doesn't exists on line ${line}`);
}

/*
 * Assert that a regular breakpoint is set in the currently
 * selected source in the editor. (no conditional, nor log breakpoint)
 *
 * @memberof mochitest/helpers
 * @param {Object} dbg
 * @param {Number} line Line where to check for a breakpoint
 * @static
 */
async function assertBreakpoint(dbg, line) {
  let el = await getNodeAtEditorGutterLine(dbg, line);
  el = isCm6Enabled ? el.firstChild : el;

  ok(
    el.classList.contains(selectors.gutterBreakpoint),
    `Breakpoint exists on line ${line}`
  );

  const hasConditionClass = el.classList.contains("has-condition");
  ok(
    !hasConditionClass,
    `Regular breakpoint doesn't have condition on line ${line}`
  );

  const hasLogClass = el.classList.contains("has-log");
  ok(!hasLogClass, `Regular breakpoint doesn't have log on line ${line}`);
}

/*
 * Assert that a conditionnal breakpoint is set.
 *
 * @memberof mochitest/helpers
 * @param {Object} dbg
 * @param {Number} line Line where to check for a breakpoint
 * @static
 */
async function assertConditionBreakpoint(dbg, line) {
  let el = await getNodeAtEditorGutterLine(dbg, line);
  el = isCm6Enabled ? el.firstChild : el;

  ok(
    el.classList.contains(selectors.gutterBreakpoint),
    `Breakpoint exists on line ${line}`
  );

  const hasConditionClass = el.classList.contains("has-condition");
  ok(hasConditionClass, `Conditional breakpoint on line ${line}`);

  const hasLogClass = el.classList.contains("has-log");
  ok(
    !hasLogClass,
    `Conditional breakpoint doesn't have log breakpoint on line ${line}`
  );
}

/*
 * Assert that a log breakpoint is set.
 *
 * @memberof mochitest/helpers
 * @param {Object} dbg
 * @param {Number} line Line where to check for a breakpoint
 * @static
 */
async function assertLogBreakpoint(dbg, line) {
  let el = await getNodeAtEditorGutterLine(dbg, line);
  el = isCm6Enabled ? el.firstChild : el;

  ok(
    el.classList.contains(selectors.gutterBreakpoint),
    `Breakpoint exists on line ${line}`
  );

  const hasConditionClass = el.classList.contains("has-condition");
  ok(
    !hasConditionClass,
    `Log breakpoint doesn't have condition on line ${line}`
  );

  const hasLogClass = el.classList.contains("has-log");
  ok(hasLogClass, `Log breakpoint on line ${line}`);
}

function assertBreakpointSnippet(dbg, index, expectedSnippet) {
  const actualSnippet = findElement(dbg, "breakpointLabel", 2).innerText;
  is(actualSnippet, expectedSnippet, `Breakpoint ${index} snippet`);
}

const selectors = {
  callStackBody: ".call-stack-pane .pane",
  domMutationItem: ".dom-mutation-list li",
  expressionNode: i =>
    `.expressions-list .expression-container:nth-child(${i}) .object-label`,
  expressionValue: i =>
    // eslint-disable-next-line max-len
    `.expressions-list .expression-container:nth-child(${i}) .object-delimiter + *`,
  expressionInput: ".watch-expressions-pane input.input-expression",
  expressionNodes: ".expressions-list .tree-node",
  expressionPlus: ".watch-expressions-pane button.plus",
  expressionRefresh: ".watch-expressions-pane button.refresh",
  expressionsHeader: ".watch-expressions-pane ._header .header-label",
  scopesHeader: ".scopes-pane ._header .header-label",
  breakpointItem: i => `.breakpoints-list div:nth-of-type(${i})`,
  breakpointLabel: i => `${selectors.breakpointItem(i)} .breakpoint-label`,
  breakpointHeadings: ".breakpoints-list .breakpoint-heading",
  breakpointItems: ".breakpoints-list .breakpoint",
  breakpointContextMenu: {
    disableSelf: "#node-menu-disable-self",
    disableAll: "#node-menu-disable-all",
    disableOthers: "#node-menu-disable-others",
    enableSelf: "#node-menu-enable-self",
    enableOthers: "#node-menu-enable-others",
    disableDbgStatement: "#node-menu-disable-dbgStatement",
    enableDbgStatement: "#node-menu-enable-dbgStatement",
    remove: "#node-menu-delete-self",
    removeOthers: "#node-menu-delete-other",
    removeCondition: "#node-menu-remove-condition",
  },
  blackboxedLines: isCm6Enabled
    ? ".cm-content > .blackboxed-line"
    : ".CodeMirror-code .blackboxed-line",
  codeLines: isCm6Enabled
    ? ".cm-content > .cm-line"
    : ".CodeMirror-code .CodeMirror-line",
  editorContextMenu: {
    continueToHere: "#node-menu-continue-to-here",
  },
  columnBreakpoints: ".column-breakpoint",
  scopes: ".scopes-list",
  scopeNodes: ".scopes-list .object-label",
  scopeNode: i => `.scopes-list .tree-node:nth-child(${i}) .object-label`,
  scopeValue: i =>
    `.scopes-list .tree-node:nth-child(${i}) .object-delimiter + *`,
  mapScopesCheckbox: ".map-scopes-header input",
  asyncframe: i =>
    `.frames div[role=listbox] .location-async-cause:nth-child(${i})`,
  frame: i => `.frames div[role=listbox] .frame:nth-child(${i})`,
  frames: ".frames [role='listbox'] .frame",
  gutterBreakpoint: isCm6Enabled ? "breakpoint-marker" : "new-breakpoint",
  // This is used to trigger events (click etc) on the gutter
  gutterElement: i =>
    isCm6Enabled
      ? `.cm-gutter.cm-lineNumbers .cm-gutterElement:nth-child(${i + 1})`
      : `.CodeMirror-code *:nth-child(${i}) .CodeMirror-linenumber`,
  gutters: isCm6Enabled ? `.cm-gutters` : `.CodeMirror-gutters`,
  line: i =>
    isCm6Enabled
      ? `.cm-content > div.cm-line:nth-child(${i})`
      : `.CodeMirror-code div:nth-child(${i}) .CodeMirror-line`,
  addConditionItem:
    "#node-menu-add-condition, #node-menu-add-conditional-breakpoint",
  editConditionItem:
    "#node-menu-edit-condition, #node-menu-edit-conditional-breakpoint",
  addLogItem: "#node-menu-add-log-point",
  editLogItem: "#node-menu-edit-log-point",
  disableItem: "#node-menu-disable-breakpoint",
  breakpoint: isCm6Enabled
    ? ".cm-gutter > .cm6-gutter-breakpoint"
    : ".CodeMirror-code > .new-breakpoint",
  highlightLine: isCm6Enabled
    ? ".cm-content > .highlight-line"
    : ".CodeMirror-code > .highlight-line",
  pausedLine: ".paused-line",
  debugErrorLine: ".new-debug-line-error",
  codeMirror: isCm6Enabled ? ".cm-editor" : ".CodeMirror",
  resume: ".resume.active",
  pause: ".pause.active",
  sourceTabs: ".source-tabs",
  activeTab: ".source-tab.active",
  stepOver: ".stepOver.active",
  stepOut: ".stepOut.active",
  stepIn: ".stepIn.active",
  prettyPrintButton: ".source-footer .prettyPrint",
  mappedSourceLink: ".source-footer .mapped-source",
  sourceMapFooterButton: ".debugger-source-map-button",
  sourceNode: i => `.sources-list .tree-node:nth-child(${i}) .node`,
  sourceNodes: ".sources-list .tree-node",
  sourceTreeThreads: '.sources-list .tree-node[aria-level="1"]',
  sourceTreeGroups: '.sources-list .tree-node[aria-level="2"]',
  sourceTreeFiles: ".sources-list .tree-node[data-expandable=false]",
  threadSourceTree: i => `.threads-list .sources-pane:nth-child(${i})`,
  sourceDirectoryLabel: i => `.sources-list .tree-node:nth-child(${i}) .label`,
  resultItems: ".result-list .result-item",
  resultItemName: (name, i) =>
    `${selectors.resultItems}:nth-child(${i})[title$="${name}"]`,
  fileMatch: ".project-text-search .line-value",
  popup: ".popover",
  previewPopup: ".preview-popup",
  openInspector: "button.open-inspector",
  outlineItem: i =>
    `.outline-list__element:nth-child(${i}) .function-signature`,
  outlineItems: ".outline-list__element",
  conditionalPanel: ".conditional-breakpoint-panel",
  conditionalPanelInput: `.conditional-breakpoint-panel  ${
    isCm6Enabled ? ".cm-content" : "textarea"
  }`,
  logPanelInput: `.conditional-breakpoint-panel.log-point ${
    isCm6Enabled ? ".cm-content" : "textarea"
  }`,
  conditionalBreakpointInSecPane: ".breakpoint.is-conditional",
  logPointPanel: ".conditional-breakpoint-panel.log-point",
  logPointInSecPane: ".breakpoint.is-log",
  searchField: ".search-field",
  blackbox: ".action.black-box",
  projectSearchSearchInput: ".project-text-search .search-field input",
  projectSearchCollapsed: ".project-text-search .arrow:not(.expanded)",
  projectSearchExpandedResults: ".project-text-search .result",
  projectSearchFileResults: ".project-text-search .file-result",
  projectSearchModifiersCaseSensitive:
    ".project-text-search button.case-sensitive-btn",
  projectSearchModifiersRegexMatch:
    ".project-text-search button.regex-match-btn",
  projectSearchModifiersWholeWordMatch:
    ".project-text-search button.whole-word-btn",
  projectSearchRefreshButton: ".project-text-search button.refresh-btn",
  threadsPaneItems: ".threads-pane .thread",
  threadsPaneItem: i => `.threads-pane .thread:nth-child(${i})`,
  threadsPaneItemPause: i => `${selectors.threadsPaneItem(i)}.paused`,
  CodeMirrorLines: isCm6Enabled ? ".cm-content" : ".CodeMirror-lines",
  CodeMirrorCode: isCm6Enabled ? ".cm-content" : ".CodeMirror-code",
  inlinePreview: isCm6Enabled
    ? ".cm-content .inline-preview"
    : ".CodeMirror-code .CodeMirror-widget",
  inlinePreviewLabels: ".inline-preview .inline-preview-label",
  inlinePreviewValues: ".inline-preview .inline-preview-value",
  inlinePreviewOpenInspector: ".inline-preview-value button.open-inspector",
  watchpointsSubmenu: "#node-menu-watchpoints",
  addGetWatchpoint: "#node-menu-add-get-watchpoint",
  logEventsCheckbox: ".events-header input",
  previewPopupInvokeGetterButton: ".preview-popup .invoke-getter",
  previewPopupObjectNumber: ".preview-popup .objectBox-number",
  previewPopupObjectObject: ".preview-popup .objectBox-object",
  previewPopupObjectFunction: ".preview-popup .objectBox-function",
  previewPopupObjectFunctionJumpToDefinition:
    ".preview-popup .objectBox-function .jump-definition",
  sourceTreeRootNode: ".sources-panel .node .window",
  sourceTreeFolderNode: ".sources-panel .node .folder",
  excludePatternsInput: ".project-text-search .exclude-patterns-field input",
  fileSearchInput: ".search-bar input",
  watchExpressionsHeader: ".watch-expressions-pane ._header .header-label",
  watchExpressionsAddButton: ".watch-expressions-pane ._header .plus",
  editorNotificationFooter: ".editor-notification-footer",
};

function getSelector(elementName, ...args) {
  let selector = selectors[elementName];
  if (!selector) {
    throw new Error(`The selector ${elementName} is not defined`);
  }

  if (typeof selector == "function") {
    selector = selector(...args);
  }

  return selector;
}

function findElement(dbg, elementName, ...args) {
  const selector = getSelector(elementName, ...args);
  return findElementWithSelector(dbg, selector);
}

function findElementWithSelector(dbg, selector) {
  return dbg.win.document.querySelector(selector);
}

function findAllElements(dbg, elementName, ...args) {
  const selector = getSelector(elementName, ...args);
  return findAllElementsWithSelector(dbg, selector);
}

function findAllElementsWithSelector(dbg, selector) {
  return dbg.win.document.querySelectorAll(selector);
}

function getSourceNodeLabel(dbg, index) {
  return findElement(dbg, "sourceNode", index)
    .textContent.trim()
    .replace(/^[\s\u200b]*/g, "");
}

/**
 * Simulates a mouse click in the debugger DOM.
 *
 * @memberof mochitest/helpers
 * @param {Object} dbg
 * @param {String} elementName
 * @param {Array} args
 * @return {Promise}
 * @static
 */
async function clickElement(dbg, elementName, ...args) {
  const selector = getSelector(elementName, ...args);
  const el = await waitForElementWithSelector(dbg, selector);

  el.scrollIntoView();

  return clickElementWithSelector(dbg, selector);
}

function clickElementWithSelector(dbg, selector) {
  clickDOMElement(dbg, findElementWithSelector(dbg, selector));
}

function clickDOMElement(dbg, element, options = {}) {
  EventUtils.synthesizeMouseAtCenter(element, options, dbg.win);
}

function dblClickElement(dbg, elementName, ...args) {
  const selector = getSelector(elementName, ...args);

  return EventUtils.synthesizeMouseAtCenter(
    findElementWithSelector(dbg, selector),
    { clickCount: 2 },
    dbg.win
  );
}

function clickElementWithOptions(dbg, elementName, options, ...args) {
  const selector = getSelector(elementName, ...args);
  const el = findElementWithSelector(dbg, selector);
  el.scrollIntoView();

  return EventUtils.synthesizeMouseAtCenter(el, options, dbg.win);
}

function altClickElement(dbg, elementName, ...args) {
  return clickElementWithOptions(dbg, elementName, { altKey: true }, ...args);
}

function shiftClickElement(dbg, elementName, ...args) {
  return clickElementWithOptions(dbg, elementName, { shiftKey: true }, ...args);
}

function rightClickElement(dbg, elementName, ...args) {
  const selector = getSelector(elementName, ...args);
  return rightClickEl(dbg, dbg.win.document.querySelector(selector));
}

function rightClickEl(dbg, el) {
  el.scrollIntoView();
  EventUtils.synthesizeMouseAtCenter(el, { type: "contextmenu" }, dbg.win);
}

async function clearElement(dbg, elementName) {
  await clickElement(dbg, elementName);
  await pressKey(dbg, "End");
  const selector = getSelector(elementName);
  const el = findElementWithSelector(dbg, getSelector(elementName));
  let len = el.value.length;
  while (len) {
    pressKey(dbg, "Backspace");
    len--;
  }
}

async function clickGutter(dbg, line) {
  const el = await (isCm6Enabled
    ? scrollAndGetEditorLineGutterElement(dbg, line)
    : codeMirrorGutterElement(dbg, line));
  clickDOMElement(dbg, el);
}

async function cmdClickGutter(dbg, line) {
  const el = await (isCm6Enabled
    ? scrollAndGetEditorLineGutterElement(dbg, line)
    : codeMirrorGutterElement(dbg, line));
  clickDOMElement(dbg, el, cmdOrCtrl);
}

function findContextMenu(dbg, selector) {
  // the context menu is in the toolbox window
  const doc = dbg.toolbox.topDoc;

  // there are several context menus, we want the one with the menu-api
  const popup = doc.querySelector('menupopup[menu-api="true"]');

  return popup.querySelector(selector);
}

async function assertContextMenuItemDisabled(dbg, selector, expectedState) {
  const item = await waitFor(() => findContextMenu(dbg, selector));
  is(item.disabled, expectedState, "The context menu item is disabled");
}

// Waits for the context menu to exist and to fully open. Once this function
// completes, selectDebuggerContextMenuItem can be called.
// waitForContextMenu must be called after menu opening has been triggered, e.g.
// after synthesizing a right click / contextmenu event.
async function waitForContextMenu(dbg) {
  // the context menu is in the toolbox window
  const doc = dbg.toolbox.topDoc;

  // there are several context menus, we want the one with the menu-api
  const popup = await waitFor(() =>
    doc.querySelector('menupopup[menu-api="true"]')
  );

  if (popup.state == "open") {
    return popup;
  }

  await new Promise(resolve => {
    popup.addEventListener("popupshown", () => resolve(), { once: true });
  });

  return popup;
}

/**
 * Closes and open context menu popup.
 *
 * @memberof mochitest/helpers
 * @param {Object} dbg
 * @param {String} popup - The currently opened popup returned by
 *                         `waitForContextMenu`.
 * @return {Promise}
 */

async function closeContextMenu(dbg, popup) {
  const onHidden = new Promise(resolve => {
    popup.addEventListener("popuphidden", resolve, { once: true });
  });
  popup.hidePopup();
  return onHidden;
}

function selectDebuggerContextMenuItem(dbg, selector) {
  const item = findContextMenu(dbg, selector);
  item.closest("menupopup").activateItem(item);
}

async function openContextMenuSubmenu(dbg, selector) {
  const item = findContextMenu(dbg, selector);
  const popup = item.menupopup;
  const popupshown = new Promise(resolve => {
    popup.addEventListener("popupshown", () => resolve(), { once: true });
  });
  item.openMenu(true);
  await popupshown;
  return popup;
}

async function assertContextMenuLabel(dbg, selector, expectedLabel) {
  const item = await waitFor(() => findContextMenu(dbg, selector));
  is(
    item.label,
    expectedLabel,
    "The label of the context menu item shown to the user"
  );
}

async function typeInPanel(dbg, text, inLogPanel = false) {
  const panelName = inLogPanel ? "logPanelInput" : "conditionalPanelInput";
  await waitForElement(dbg, panelName);

  // Wait a bit for panel's codemirror document to complete any updates
  // so the  input does not lose focus after the it has been opened
  await waitForInPanelDocumentLoadComplete(dbg, panelName);

  // Position cursor reliably at the end of the text.
  pressKey(dbg, "End");

  type(dbg, text);
  // Wait for any possible CM6 scroll actions in the conditional panel editor
  // to complete
  if (isCm6Enabled) {
    await wait(1000);
  }
  pressKey(dbg, "Enter");
}

async function toggleMapScopes(dbg) {
  info("Turn on original variable mapping");
  const scopesLoaded = waitForLoadedScopes(dbg);
  const onDispatch = waitForDispatch(dbg.store, "TOGGLE_MAP_SCOPES");
  clickElement(dbg, "mapScopesCheckbox");
  return Promise.all([onDispatch, scopesLoaded]);
}

async function waitForPausedInOriginalFileAndToggleMapScopes(
  dbg,
  expectedSelectedSource = null
) {
  // Original variable mapping is not switched on, so do not wait for any loaded scopes
  await waitForPaused(dbg, expectedSelectedSource, {
    shouldWaitForLoadedScopes: false,
  });
  await toggleMapScopes(dbg);
}

function toggleExpressions(dbg) {
  return findElement(dbg, "expressionsHeader").click();
}

function toggleScopes(dbg) {
  return findElement(dbg, "scopesHeader").click();
}

function toggleExpressionNode(dbg, index) {
  return toggleObjectInspectorNode(findElement(dbg, "expressionNode", index));
}

function toggleScopeNode(dbg, index) {
  return toggleObjectInspectorNode(findElement(dbg, "scopeNode", index));
}

function rightClickScopeNode(dbg, index) {
  rightClickObjectInspectorNode(dbg, findElement(dbg, "scopeNode", index));
}

function getScopeNodeLabel(dbg, index) {
  return findElement(dbg, "scopeNode", index).innerText;
}

function getScopeNodeValue(dbg, index) {
  return findElement(dbg, "scopeValue", index).innerText;
}

function toggleObjectInspectorNode(node) {
  const objectInspector = node.closest(".object-inspector");
  const properties = objectInspector.querySelectorAll(".node").length;

  info(`Toggle node ${node.innerText}`);
  node.click();

  info(`Waiting for object inspector properties update`);
  return waitUntil(
    () => objectInspector.querySelectorAll(".node").length !== properties
  );
}

function rightClickObjectInspectorNode(dbg, node) {
  const objectInspector = node.closest(".object-inspector");
  const properties = objectInspector.querySelectorAll(".node").length;

  info(`Right clicking node ${node.innerText}`);
  rightClickEl(dbg, node);

  info(`Waiting for object inspector properties update`);
  return waitUntil(
    () => objectInspector.querySelectorAll(".node").length !== properties
  );
}

/*******************************************
 * Utilities for handling codemirror
 ******************************************/

// Gets the current source editor for CM6 tests
function getCMEditor(dbg) {
  return dbg.win.codeMirrorSourceEditorTestInstance;
}

function wasmOffsetToLine(dbg, sourceId, offset) {
  if (isCm6Enabled) {
    return getCMEditor(dbg).wasmOffsetToLine(offset) + 1;
  }
  return dbg.wasmOffsetToLine(sourceId, offset) + 1;
}

// Gets the number of lines in the editor
function getLineCount(dbg) {
  return getCMEditor(dbg).getLineCount();
}

/**
 * Wait for CodeMirror to start searching
 */
function waitForSearchState(dbg) {
  return waitFor(() => getCMEditor(dbg).isSearchStateReady());
}

/**
 * Wait for the document of the main debugger editor codemirror instance
 * to completely load (for CM6 only)
 */
function waitForDocumentLoadComplete(dbg) {
  return waitFor(() =>
    isCm6Enabled ? getCMEditor(dbg).codeMirror.isDocumentLoadComplete : true
  );
}

/**
 * Wait for the document of the conditional/log point panel's codemirror instance
 * to completely load (for CM6 only)
 */
function waitForInPanelDocumentLoadComplete(dbg, panelName) {
  return waitFor(() =>
    isCm6Enabled
      ? getCodeMirrorInstance(dbg, panelName).isDocumentLoadComplete
      : true
  );
}

/**
 * Gets the content for the editor as a string. it uses the
 * newline character to separate lines.
 */
function getEditorContent(dbg) {
  return getCMEditor(dbg).getEditorContent();
}

/**
 * Retrieve the codemirror instance for the provided debugger instance.
 * Optionally provide a panel name such as "logPanelInput" or
 * "conditionalPanelInput" to retrieve the codemirror instances specific to
 * those panels.
 *
 * @param {Object} dbg
 * @param {string} panelName
 * @returns {CodeMirror}
 *     The codemirror instance corresponding to the provided debugger and panel name.
 */
function getCodeMirrorInstance(dbg, panelName = null) {
  if (panelName !== null) {
    const panel = findElement(dbg, panelName);
    return dbg.win.codeMirrorSourceEditorTestInstance.CodeMirror.findFromDOM(
      panel
    );
  }
  return dbg.win.codeMirrorSourceEditorTestInstance.codeMirror;
}

/**
 * Set the cursor  at a specific location in the editor
 * @param {*} dbg
 * @param {Number} line
 * @param {Number} column
 * @returns
 */
function setEditorCursorAt(dbg, line, column) {
  scrollEditorIntoView(dbg, line, 0);
  return getCMEditor(dbg).setCursorAt(line, column);
}

/**
 * Scrolls a specific line and column into view in the editor
 *
 * @param {*} dbg
 * @param {Number} line
 * @param {Number} column
 * @param {String|null} yAlign
 * @returns
 */
async function scrollEditorIntoView(dbg, line, column, yAlign) {
  const onScrolled = waitForScrolling(dbg);
  line = isCm6Enabled ? line + 1 : line;
  getCMEditor(dbg).scrollTo(line, column, yAlign);
  // Ensure the line is visible with margin because the bar at the bottom of
  // the editor overlaps into what the editor thinks is its own space, blocking
  // the click event below.
  return onScrolled;
}

/**
 * Wrapper around source editor api to check if a scrolled position is visible
 *
 * @param {*} dbg
 * @param {Number} line 1-based
 * @param {Number} column
 * @returns
 */
function isScrolledPositionVisible(dbg, line, column = 0) {
  // CodeMirror 6 uses 1-based lines whereas 5 uses 0-based.
  line = isCm6Enabled ? line : line - 1;
  return getCMEditor(dbg).isPositionVisible(line, column);
}

function setSelection(dbg, startLine, endLine) {
  getCMEditor(dbg).setSelectionAt(
    { line: startLine, column: 0 },
    { line: endLine, column: 0 }
  );
}

function getSearchQuery(dbg) {
  return getCMEditor(dbg).getSearchQuery();
}

function getSearchSelection(dbg) {
  return getCMEditor(dbg).getSearchSelection();
}

// Gets the mode used for the file
function getEditorFileMode(dbg) {
  return getCMEditor(dbg).getEditorFileMode();
}

function getCoordsFromPosition(dbg, line, ch) {
  return getCMEditor(dbg).getCoords(line, ch);
}

async function getTokenFromPosition(dbg, { line, column = 0 }) {
  info(`Get token at ${line}:${column}`);
  await scrollEditorIntoView(dbg, line, column);
  line = isCm6Enabled ? line : line - 1;
  column = isCm6Enabled ? column : column - 1;

  if (isCm6Enabled) {
    return getCMEditor(dbg).getElementAtPos(line, column);
  }

  const { left, top } = getCoordsFromPosition(dbg, line, column);

  // Adds a vertical offset due to increased line height
  // https://github.com/firefox-devtools/debugger/pull/7934
  const lineHeightOffset = 3;

  // Note that we might end up retrieving any popup if one is still shown over the expected token
  return dbg.win.document.elementFromPoint(left, top + lineHeightOffset);
}
/**
 * Waits for the currently triggered scroll to complete
 *
 * @param {*} dbg
 * @param {Object} options
 * @param {Boolean} options.useTimeoutFallback - defaults to true. When set to false
 *                                               a scroll must happen for the wait for scrolling to complete
 * @returns
 */
async function waitForScrolling(dbg, { useTimeoutFallback = true } = {}) {
  return new Promise(resolve => {
    const editor = getCMEditor(dbg);
    if (isCm6Enabled) {
      editor.once("cm-editor-scrolled", resolve);
    } else {
      function onScroll() {
        editor.codeMirror.off("scroll", onScroll);
        resolve();
      }
      editor.codeMirror.on("scroll", onScroll);
    }
    if (useTimeoutFallback) {
      setTimeout(resolve, 500);
    }
  });
}

async function codeMirrorGutterElement(dbg, line) {
  info(`CodeMirror line ${line}`);

  await scrollEditorIntoView(dbg, line, 0);
  line = isCm6Enabled ? line : line - 1;

  const coords = getCoordsFromPosition(dbg, line);

  const { left, top } = coords;

  // Adds a vertical offset due to increased line height
  // https://github.com/firefox-devtools/debugger/pull/7934
  const lineHeightOffset = 3;

  // Click in the center of the line/breakpoint
  const leftOffset = 10;

  const tokenEl = dbg.win.document.elementFromPoint(
    left - leftOffset,
    top + lineHeightOffset
  );

  if (!tokenEl) {
    throw new Error(`Failed to find element for line ${line}`);
  }
  return tokenEl;
}

async function clickAtPos(dbg, pos) {
  const tokenEl = await getTokenFromPosition(dbg, pos);

  if (!tokenEl) {
    return;
  }

  const { top, left } = tokenEl.getBoundingClientRect();
  info(
    `Clicking on token ${tokenEl.innerText} in line ${tokenEl.parentNode.innerText}`
  );
  // TODO: Unify the usage for CM6 and CM5 Bug 1919694
  if (isCm6Enabled) {
    EventUtils.synthesizeMouseAtCenter(tokenEl, {}, dbg.win);
  } else {
    tokenEl.dispatchEvent(
      new PointerEvent("click", {
        bubbles: true,
        cancelable: true,
        view: dbg.win,
        // Shift by one as we might be on the edge of the element and click on previous line/column
        clientX: left + 1,
        clientY: top + 1,
      })
    );
  }
}

async function rightClickAtPos(dbg, pos) {
  const el = await getTokenFromPosition(dbg, pos);
  if (!el) {
    return;
  }
  // In CM6 when clicking in the editor an extra click is needed
  // TODO: Investiaget and remove Bug 1919693
  if (isCm6Enabled) {
    EventUtils.synthesizeMouseAtCenter(el, {}, dbg.win);
  }
  rightClickEl(dbg, el);
}

async function hoverAtPos(dbg, pos) {
  const tokenEl = await getTokenFromPosition(dbg, pos);

  if (!tokenEl) {
    return;
  }

  hoverToken(tokenEl);
}

function hoverToken(tokenEl) {
  info(`Hovering on token <${tokenEl.innerText}>`);

  // We can't use synthesizeMouse(AtCenter) as it's using the element bounding client rect.
  // But here, we might have a token that wraps on multiple line and the center of the
  // bounding client rect won't actually hover the token.
  // +───────────────────────+
  // │      myLongVariableNa│
  // │me         +          │
  // +───────────────────────+

  // Instead, we need to get the first quad.
  const { p1, p2, p3 } = tokenEl.getBoxQuads()[0];
  const x = p1.x + (p2.x - p1.x) / 2;
  const y = p1.y + (p3.y - p1.y) / 2;

  // This first event helps utils/editor/tokens.js to receive the right mouseover event
  EventUtils.synthesizeMouseAtPoint(
    x,
    y,
    {
      type: "mouseover",
    },
    tokenEl.ownerGlobal
  );

  // This second event helps Popover to have :hover pseudoclass set on the token element
  EventUtils.synthesizeMouseAtPoint(
    x,
    y,
    {
      type: "mousemove",
    },
    tokenEl.ownerGlobal
  );
}

/**
 * Helper to close a variable preview popup.
 *
 * @param {Object} dbg
 * @param {DOM Element} tokenEl
 *        The DOM element on which we hovered to display the popup.
 * @param {String} previewType
 *        Based on the actual JS value being hovered we may have two different kinds
 *        of popups: popup (for js objects) or previewPopup (for primitives)
 */
async function closePreviewForToken(
  dbg,
  tokenEl,
  previewType = "previewPopup"
) {
  ok(
    findElement(dbg, previewType),
    "A preview was opened before trying to close it"
  );

  // Force "mousing out" from all elements.
  //
  // This helps utils/editor/tokens.js to receive the right mouseleave event.
  // This is super important as it will then allow re-emitting a tokenenter event if you try to re-preview the same token!
  // We can't use synthesizeMouse(AtCenter) as it's using the element bounding client rect.
  // But here, we might have a token that wraps on multiple line and the center of the
  // bounding client rect won't actually hover the token.
  // +───────────────────────+
  // │      myLongVariableNa│
  // │me         +          │
  // +───────────────────────+

  // Instead, we need to get the first quad.
  const { p1, p2, p3 } = tokenEl.getBoxQuads()[0];
  const x = p1.x + (p2.x - p1.x) / 2;
  const y = p1.y + (p3.y - p1.y) / 2;
  EventUtils.synthesizeMouseAtPoint(
    tokenEl,
    x,
    y,
    {
      type: "mouseout",
    },
    tokenEl.ownerGlobal
  );

  // This second event helps Popover to have :hover pseudoclass removed on the token element
  //
  // For some unexplained reason, the precise element onto which we emit mousemove is actually important.
  // Emitting it on documentElement, or random other element within CodeMirror would cause
  // a "mousemove" event to be emitted on preview-popup element instead and wouldn't cause :hover
  // pseudoclass to be dropped.
  const element = tokenEl.ownerDocument.querySelector(
    ".debugger-settings-menu-button"
  );
  EventUtils.synthesizeMouseAtCenter(
    element,
    {
      type: "mousemove",
    },
    element.ownerGlobal
  );

  info(`Waiting for preview to be closed (preview type=${previewType})`);
  await waitUntil(() => findElement(dbg, previewType) == null);
  info("Preview closed");
}

/**
 * Hover at a position until we see a preview element (popup, tooltip) appear.
 * ⚠️ Note that this is using CodeMirror method to retrieve the token element
 * and that could be subject to CodeMirror bugs / outdated internal state
 *
 * @param {Debugger} dbg
 * @param {Integer} line: The line we want to hover over
 * @param {Integer} column: The column we want to hover over
 * @param {String} elementName: "Selector" string that will be passed to waitForElement,
 *                              describing the element that should be displayed on hover.
 * @returns Promise<{element, tokenEl}>
 *          element is the DOM element matching the passed elementName
 *          tokenEl is the DOM element for the token we hovered
 */
async function tryHovering(dbg, line, column, elementName) {
  ok(
    !findElement(dbg, elementName),
    "The expected preview element on hover should not exist beforehand"
  );
  // Wait for all the updates to the document to complete to make all
  // token elements have been rendered
  await waitForDocumentLoadComplete(dbg);
  const tokenEl = await getTokenFromPosition(dbg, { line, column });
  return tryHoverToken(dbg, tokenEl, elementName);
}

/**
 * Retrieve the token element matching `expression` at line `line` and hover it.
 * This is retrieving the token from the DOM, contrary to `tryHovering`, which calls
 * CodeMirror internal method for this (and which might suffer from bugs / outdated internal state)
 *
 * @param {Debugger} dbg
 * @param {String} expression: The text of the token we want to hover
 * @param {Integer} line: The line the token should be at
 * @param {Integer} column: The column the token should be at
 * @param {String} elementName: "Selector" string that will be passed to waitForElement,
 *                              describing the element that should be displayed on hover.
 * @returns Promise<{element, tokenEl}>
 *          element is the DOM element matching the passed elementName
 *          tokenEl is the DOM element for the token we hovered
 */
async function tryHoverTokenAtLine(dbg, expression, line, column, elementName) {
  info("Scroll codeMirror to make the token visible");
  await scrollEditorIntoView(dbg, line, 0);
  // Wait for all the updates to the document to complete to make all
  // token elements have been rendered
  await waitForDocumentLoadComplete(dbg);
  // Lookup for the token matching the passed expression
  const tokenEl = await getTokenElAtLine(dbg, expression, line, column);
  if (!tokenEl) {
    throw new Error(
      `Couldn't find token <${expression}> on ${line}:${column}\n`
    );
  }

  ok(true, `Found token <${expression}> on ${line}:${column}`);

  return tryHoverToken(dbg, tokenEl, elementName);
}

async function tryHoverToken(dbg, tokenEl, elementName) {
  hoverToken(tokenEl);

  // Wait for the preview element to be created
  const element = await waitForElement(dbg, elementName);
  return { element, tokenEl };
}

/**
 * Retrieve the token element matching `expression` at line `line`, from the DOM.
 *
 * @param {Debugger} dbg
 * @param {String} expression: The text of the token we want to hover
 * @param {Integer} line: The line the token should be at
 * @param {Integer} column: The column the token should be at
 * @returns {Element} the token element, or null if not found
 */
async function getTokenElAtLine(dbg, expression, line, column = 0) {
  info(`Search for <${expression}> token on ${line}:${column}`);
  let editorLineEl;
  if (isCm6Enabled) {
    // Get the related editor line
    editorLineEl = getCMEditor(dbg).getElementAtLine(line);
  } else {
    // Get the line gutter element matching the passed line
    const lineGutterEl = [
      ...dbg.win.document.querySelectorAll(".CodeMirror-linenumber"),
    ].find(el => el.textContent === `${line}`);

    // Get the related editor line
    editorLineEl = lineGutterEl
      .closest(".CodeMirror-gutter-wrapper")
      .parentElement.querySelector(".CodeMirror-line");
  }

  // Lookup for the token matching the passed expression
  const tokenParent = isCm6Enabled
    ? editorLineEl
    : editorLineEl.querySelector(".CodeMirror-line > span");

  const tokenElements = [...tokenParent.childNodes];
  let currentColumn = 1;
  return tokenElements.find(el => {
    const childText = el.textContent;
    currentColumn += childText.length;

    // Only consider elements that are after the passed column
    if (currentColumn < column) {
      return false;
    }
    return childText == expression;
  });
}

/**
 * Wait for a few ms and assert that a tooltip preview was not displayed.
 * @param {*} dbg
 */
async function assertNoTooltip(dbg) {
  await wait(200);
  const el = findElement(dbg, "previewPopup");
  is(el, null, "Tooltip should not exist");
}

/**
 * Hovers and asserts tooltip previews with simple text expressions (i.e numbers and strings)
 * @param {*} dbg
 * @param {Number} line
 * @param {Number} column
 * @param {Object} options
 * @param {String}  options.result - Expected text shown in the preview
 * @param {String}  options.expression - The expression hovered over
 * @param {Boolean} options.doNotClose - Set to true to not close the tooltip
 */
async function assertPreviewTextValue(
  dbg,
  line,
  column,
  { result, expression, doNotClose = false }
) {
  // CodeMirror refreshes after inline previews are displayed, so wait until they're rendered.
  await waitForInlinePreviews(dbg);

  const { element: previewEl, tokenEl } = await tryHoverTokenAtLine(
    dbg,
    expression,
    line,
    column,
    "previewPopup"
  );

  ok(
    previewEl.innerText.includes(result),
    "Popup preview text shown to user. Got: " +
      previewEl.innerText +
      " Expected: " +
      result
  );

  if (!doNotClose) {
    await closePreviewForToken(dbg, tokenEl);
  }
}

/**
 * Asserts multiple previews
 * @param {*} dbg
 * @param {Array} previews
 */
async function assertPreviews(dbg, previews) {
  // Move the cursor to the top left corner to have a clean state
  EventUtils.synthesizeMouse(
    findElement(dbg, "codeMirror"),
    0,
    0,
    {
      type: "mousemove",
    },
    dbg.win
  );

  // CodeMirror refreshes after inline previews are displayed, so wait until they're rendered.
  await waitForInlinePreviews(dbg);

  for (const { line, column, expression, result, header, fields } of previews) {
    info(" # Assert preview on " + line + ":" + column);

    if (result) {
      await assertPreviewTextValue(dbg, line, column, {
        expression,
        result,
      });
    }

    if (fields) {
      const { element: popupEl, tokenEl } = expression
        ? await tryHoverTokenAtLine(dbg, expression, line, column, "popup")
        : await tryHovering(dbg, line, column, "popup");

      info("Wait for child nodes to load");
      await waitUntil(
        () => popupEl.querySelectorAll(".preview-popup .node").length > 1
      );
      ok(true, "child nodes loaded");

      const oiNodes = Array.from(
        popupEl.querySelectorAll(".preview-popup .node")
      );

      if (header) {
        is(
          oiNodes[0].querySelector(".objectBox").textContent,
          header,
          "popup has expected value"
        );
      }

      for (const [field, value] of fields) {
        const node = oiNodes.find(
          oiNode => oiNode.querySelector(".object-label")?.textContent === field
        );
        if (!node) {
          ok(false, `The "${field}" property is not displayed in the popup`);
        } else {
          is(
            node.querySelector(".object-label").textContent,
            field,
            `The "${field}" property is displayed in the popup`
          );
          if (value !== undefined) {
            is(
              node.querySelector(".objectBox").textContent,
              value,
              `The "${field}" property has the expected value`
            );
          }
        }
      }

      await closePreviewForToken(dbg, tokenEl, "popup");
    }
  }
}

/**
 * Asserts the inline expression preview value
 * @param {*} dbg
 * @param {Number} line
 * @param {Number} column
 * @param {Object} options
 * @param {String}  options.result - Expected text shown in the preview
 * @param {Array}  options.fields - The expected stacktrace information
 */
async function assertInlineExceptionPreview(
  dbg,
  line,
  column,
  { result, fields }
) {
  info(" # Assert preview on " + line + ":" + column);
  const { element: popupEl, tokenEl } = await tryHovering(
    dbg,
    line,
    column,
    "previewPopup"
  );

  info("Wait for top level node to expand and child nodes to load");
  await waitForElementWithSelector(
    dbg,
    ".exception-popup .exception-message .arrow.expanded"
  );

  is(
    popupEl.querySelector(".preview-popup .exception-message .objectBox")
      .textContent,
    result,
    "The correct result is not displayed in the popup"
  );

  await waitFor(() =>
    popupEl.querySelectorAll(".preview-popup .exception-stacktrace .frame")
  );
  const stackFrameNodes = Array.from(
    popupEl.querySelectorAll(".preview-popup .exception-stacktrace .frame")
  );

  for (const [field, value] of fields) {
    const node = stackFrameNodes.find(
      frameNode => frameNode.querySelector(".title")?.textContent === field
    );
    if (!node) {
      ok(false, `The "${field}" property is not displayed in the popup`);
    } else {
      is(
        node.querySelector(".location").textContent,
        value,
        `The "${field}" property has the expected value`
      );
    }
  }

  await closePreviewForToken(dbg, tokenEl, "previewPopup");
}

/**
 * Wait until a preview popup containing the given result is shown
 * @param {*} dbg
 * @param {String} result
 */
async function waitForPreviewWithResult(dbg, result) {
  info(`Wait for preview popup with result ${result}`);
  await waitUntil(async () => {
    const previewEl = await waitForElement(dbg, "previewPopup");
    return previewEl.innerText.includes(result);
  });
}

/**
 * Expand or collapse a node in the preview popup
 * @param {*} dbg
 * @param {Number} index
 */
async function toggleExpanded(dbg, index) {
  let initialNodesLength;
  await waitFor(() => {
    const nodes = findElement(dbg, "previewPopup")?.querySelectorAll(".node");
    if (nodes?.length > index) {
      initialNodesLength = nodes.length;
      nodes[index].querySelector(".theme-twisty").click();
      return true;
    }
    return false;
  });
  await waitFor(
    () =>
      findElement(dbg, "previewPopup").querySelectorAll(".node").length !==
      initialNodesLength
  );
}

async function waitForBreakableLine(dbg, source, lineNumber) {
  await waitForState(
    dbg,
    () => {
      const currentSource = findSource(dbg, source);

      const breakableLines =
        currentSource && dbg.selectors.getBreakableLines(currentSource.id);

      return breakableLines && breakableLines.includes(lineNumber);
    },
    `waiting for breakable line ${lineNumber}`
  );
}

async function waitForSourceTreeThreadsCount(dbg, i) {
  info(`Waiting for ${i} threads in the source tree`);
  await waitUntil(() => {
    return findAllElements(dbg, "sourceTreeThreads").length === i;
  });
}

function getDisplayedSourceElements(dbg) {
  return [...findAllElements(dbg, "sourceTreeFiles")];
}

function getDisplayedSources(dbg) {
  return getDisplayedSourceElements(dbg).map(e => {
    // Replace some non visible space characters that prevents Array.includes from working correctly
    return e.textContent.trim().replace(/^[\s\u200b]*/g, "");
  });
}

/**
 * Wait for a single source to be visible in the Source Tree.
 */
async function waitForSourceInSourceTree(dbg, fileName) {
  return waitFor(
    async () => {
      await expandSourceTree(dbg);

      return getDisplayedSourceElements(dbg).find(e => {
        // Replace some non visible space characters that prevents Array.includes from working correctly
        return e.textContent.trim().replace(/^[\s\u200b]*/g, "") == fileName;
      });
    },
    null,
    100,
    50
  );
}

/**
 * Wait for a precise list of sources to be shown in the Source Tree.
 * No more, no less than the list.
 */
async function waitForSourcesInSourceTree(
  dbg,
  sources,
  { noExpand = false } = {}
) {
  info(`waiting for ${sources.length} files in the source tree`);
  try {
    // Use custom timeout and retry count for waitFor as the test method is slow to resolve
    // and default value makes the timeout unecessarily long
    await waitFor(
      async () => {
        if (!noExpand) {
          await expandSourceTree(dbg);
        }
        const displayedSources = getDisplayedSources(dbg);
        return (
          displayedSources.length == sources.length &&
          sources.every(source => displayedSources.includes(source))
        );
      },
      null,
      100,
      50
    );
  } catch (e) {
    // Craft a custom error message to help understand what's wrong with the Source Tree content
    const displayedSources = getDisplayedSources();
    let msg = "Invalid Source Tree Content.\n";
    const missingElements = [];
    for (const source of sources) {
      const idx = displayedSources.indexOf(source);
      if (idx != -1) {
        displayedSources.splice(idx, 1);
      } else {
        missingElements.push(source);
      }
    }
    if (missingElements.length) {
      msg += "Missing elements: " + missingElements.join(", ") + "\n";
    }
    if (displayedSources.length) {
      msg += "Unexpected elements: " + displayedSources.join(", ");
    }
    throw new Error(msg);
  }
}

async function waitForNodeToGainFocus(dbg, index) {
  info(`Waiting for source node #${index} to be focused`);
  await waitUntil(() => {
    const element = findElement(dbg, "sourceNode", index);

    if (element) {
      return element.classList.contains("focused");
    }

    return false;
  });
}

async function assertNodeIsFocused(dbg, index) {
  await waitForNodeToGainFocus(dbg, index);
  const node = findElement(dbg, "sourceNode", index);
  ok(node.classList.contains("focused"), `node ${index} is focused`);
}

/**
 * Asserts that the debugger is paused and the debugger tab is
 * highlighted.
 * @param {*} toolbox
 * @returns
 */
async function assertDebuggerIsHighlightedAndPaused(toolbox) {
  info("Wait for the debugger to be automatically selected on pause");
  await waitUntil(() => toolbox.currentToolId == "jsdebugger");
  ok(true, "Debugger selected");

  // Wait for the debugger to finish loading.
  await toolbox.getPanelWhenReady("jsdebugger");

  // And to be fully paused
  const dbg = createDebuggerContext(toolbox);
  await waitForPaused(dbg);

  ok(toolbox.isHighlighted("jsdebugger"), "Debugger is highlighted");

  return dbg;
}

async function addExpression(dbg, input) {
  info("Adding an expression");

  const plusIcon = findElement(dbg, "expressionPlus");
  if (plusIcon) {
    plusIcon.click();
  }
  findElement(dbg, "expressionInput").focus();
  type(dbg, input);
  const evaluated = waitForDispatch(dbg.store, "EVALUATE_EXPRESSION");
  const clearAutocomplete = waitForDispatch(dbg.store, "CLEAR_AUTOCOMPLETE");
  pressKey(dbg, "Enter");
  await evaluated;
  await clearAutocomplete;
}

async function editExpression(dbg, input) {
  info("Updating the expression");
  dblClickElement(dbg, "expressionNode", 1);
  // Position cursor reliably at the end of the text.
  pressKey(dbg, "End");
  type(dbg, input);
  const evaluated = waitForDispatch(dbg.store, "EVALUATE_EXPRESSIONS");
  pressKey(dbg, "Enter");
  await evaluated;
}

/**
 * Get the text representation of a watch expression label given its position in the panel
 *
 * @param {Object} dbg
 * @param {Number} index: Position in the panel of the expression we want the label of
 * @returns {String}
 */
function getWatchExpressionLabel(dbg, index) {
  return findElement(dbg, "expressionNode", index).innerText;
}

/**
 * Get the text representation of a watch expression value given its position in the panel
 *
 * @param {Object} dbg
 * @param {Number} index: Position in the panel of the expression we want the value of
 * @returns {String}
 */
function getWatchExpressionValue(dbg, index) {
  return findElement(dbg, "expressionValue", index).innerText;
}

// Return a promise with a reference to jsterm, opening the split
// console if necessary.  This cleans up the split console pref so
// it won't pollute other tests.
async function getDebuggerSplitConsole(dbg) {
  let { toolbox, win } = dbg;

  if (!win) {
    win = toolbox.win;
  }

  if (!toolbox.splitConsole) {
    pressKey(dbg, "Escape");
  }

  await toolbox.openSplitConsole();
  return toolbox.getPanel("webconsole");
}

// Return a promise that resolves with the result of a thread evaluating a
// string in the topmost frame.
async function evaluateInTopFrame(dbg, text) {
  const threadFront = dbg.toolbox.target.threadFront;
  const { frames } = await threadFront.getFrames(0, 1);
  ok(frames.length == 1, "Got one frame");
  const response = await dbg.commands.scriptCommand.execute(text, {
    frameActor: frames[0].actorID,
  });
  return response.result.type == "undefined" ? undefined : response.result;
}

// Return a promise that resolves when a thread evaluates a string in the
// topmost frame, ensuring the result matches the expected value.
async function checkEvaluateInTopFrame(dbg, text, expected) {
  const rval = await evaluateInTopFrame(dbg, text);
  ok(rval == expected, `Eval returned ${expected}`);
}

async function findConsoleMessage({ toolbox }, query) {
  const [message] = await findConsoleMessages(toolbox, query);
  const value = message.querySelector(".message-body").innerText;
  // There are console messages which might not have a link e.g Error messages
  const link = message.querySelector(".frame-link-source")?.innerText;
  return { value, link };
}

async function findConsoleMessages(toolbox, query) {
  const webConsole = await toolbox.getPanel("webconsole");
  const win = webConsole._frameWindow;
  return Array.prototype.filter.call(
    win.document.querySelectorAll(".message"),
    e => e.innerText.includes(query)
  );
}

async function hasConsoleMessage({ toolbox }, msg) {
  return waitFor(async () => {
    const messages = await findConsoleMessages(toolbox, msg);
    return !!messages.length;
  });
}

function evaluateExpressionInConsole(
  hud,
  expression,
  expectedClassName = "result"
) {
  const seenMessages = new Set(
    JSON.parse(
      hud.ui.outputNode
        .querySelector("[data-visible-messages]")
        .getAttribute("data-visible-messages")
    )
  );
  const onResult = new Promise(res => {
    const onNewMessage = messages => {
      for (const message of messages) {
        if (
          message.node.classList.contains(expectedClassName) &&
          !seenMessages.has(message.node.getAttribute("data-message-id"))
        ) {
          hud.ui.off("new-messages", onNewMessage);
          res(message.node);
        }
      }
    };
    hud.ui.on("new-messages", onNewMessage);
  });
  hud.ui.wrapper.dispatchEvaluateExpression(expression);
  return onResult;
}

function waitForInspectorPanelChange(dbg) {
  return dbg.toolbox.getPanelWhenReady("inspector");
}

function getEagerEvaluationElement(hud) {
  return hud.ui.outputNode.querySelector(".eager-evaluation-result");
}

async function waitForEagerEvaluationResult(hud, text) {
  info(`Waiting for eager evaluation result: ${text}`);
  await waitUntil(() => {
    const elem = getEagerEvaluationElement(hud);
    if (elem) {
      if (text instanceof RegExp) {
        return text.test(elem.innerText);
      }
      return elem.innerText == text;
    }
    return false;
  });
  ok(true, `Got eager evaluation result ${text}`);
}

function setInputValue(hud, value) {
  const onValueSet = hud.jsterm.once("set-input-value");
  hud.jsterm._setValue(value);
  return onValueSet;
}

function assertMenuItemChecked(menuItem, isChecked) {
  is(
    !!menuItem.getAttribute("aria-checked"),
    isChecked,
    `Item has expected state: ${isChecked ? "checked" : "unchecked"}`
  );
}

async function toggleDebbuggerSettingsMenuItem(dbg, { className, isChecked }) {
  const menuButton = findElementWithSelector(
    dbg,
    ".command-bar .debugger-settings-menu-button"
  );
  const { parent } = dbg.panel.panelWin;
  const { document } = parent;

  menuButton.click();
  // Waits for the debugger settings panel to appear.
  await waitFor(() => {
    const menuListEl = document.querySelector("#debugger-settings-menu-list");
    // Lets check the offsetParent property to make sure the menu list is actually visible
    // by its parents display property being no longer "none".
    return menuListEl && menuListEl.offsetParent !== null;
  });

  const menuItem = document.querySelector(className);

  assertMenuItemChecked(menuItem, isChecked);

  menuItem.click();

  // Waits for the debugger settings panel to disappear.
  await waitFor(() => menuButton.getAttribute("aria-expanded") === "false");
}

async function toggleSourcesTreeSettingsMenuItem(
  dbg,
  { className, isChecked }
) {
  const menuButton = findElementWithSelector(
    dbg,
    ".sources-list .debugger-settings-menu-button"
  );
  const { parent } = dbg.panel.panelWin;
  const { document } = parent;

  menuButton.click();
  // Waits for the debugger settings panel to appear.
  await waitFor(() => {
    const menuListEl = document.querySelector(
      "#sources-tree-settings-menu-list"
    );
    // Lets check the offsetParent property to make sure the menu list is actually visible
    // by its parents display property being no longer "none".
    return menuListEl && menuListEl.offsetParent !== null;
  });

  const menuItem = document.querySelector(className);

  assertMenuItemChecked(menuItem, isChecked);

  menuItem.click();

  // Waits for the debugger settings panel to disappear.
  await waitFor(() => menuButton.getAttribute("aria-expanded") === "false");
}

/**
 * Click on the source map button in the editor's footer
 * and wait for its context menu to be rendered before clicking
 * on one menuitem of it.
 *
 * @param {Object} dbg
 * @param {String} className
 *        The class name of the menuitem to click in the context menu.
 */
async function clickOnSourceMapMenuItem(dbg, className) {
  const menuButton = findElement(dbg, "sourceMapFooterButton");
  const { parent } = dbg.panel.panelWin;
  const { document } = parent;

  menuButton.click();
  // Waits for the debugger settings panel to appear.
  await waitFor(() => {
    const menuListEl = document.querySelector("#debugger-source-map-list");
    // Lets check the offsetParent property to make sure the menu list is actually visible
    // by its parents display property being no longer "none".
    return menuListEl && menuListEl.offsetParent !== null;
  });

  const menuItem = document.querySelector(className);
  menuItem.click();
}

async function setLogPoint(dbg, index, value) {
  // Wait a bit for CM6 to complete any updates so the log panel
  // does not lose focus after the it has been opened
  await waitForDocumentLoadComplete(dbg);
  rightClickElement(dbg, "gutterElement", index);
  await waitForContextMenu(dbg);
  selectDebuggerContextMenuItem(
    dbg,
    `${selectors.addLogItem},${selectors.editLogItem}`
  );
  await waitForConditionalPanelFocus(dbg);
  if (value) {
    const onBreakpointSet = waitForDispatch(dbg.store, "SET_BREAKPOINT");
    await typeInPanel(dbg, value, true);
    await onBreakpointSet;
  }
}
/**
 * Opens the project search panel
 *
 * @param {Object} dbg
 * @return {Boolean} The project search is open
 */
function openProjectSearch(dbg) {
  info("Opening the project search panel");
  synthesizeKeyShortcut("CmdOrCtrl+Shift+F");
  return waitForState(dbg, () => dbg.selectors.getActiveSearch() === "project");
}

/**
 * Starts a project search based on the specified search term
 *
 * @param {Object} dbg
 * @param {String} searchTerm - The test to search for
 * @param {Number} expectedResults - The expected no of results to wait for.
 *                                   This is the number of file results and not the numer of matches in all files.
 *                                   When falsy value is passed, expects no match.
 * @return {Array} List of search results element nodes
 */
async function doProjectSearch(dbg, searchTerm, expectedResults) {
  await clearElement(dbg, "projectSearchSearchInput");
  type(dbg, searchTerm);
  pressKey(dbg, "Enter");
  return waitForSearchResults(dbg, expectedResults);
}

/**
 * Waits for the search results node to render
 *
 * @param {Object} dbg
 * @param {Number} expectedResults - The expected no of results to wait for
 *                                   This is the number of file results and not the numer of matches in all files.
 * @return (Array) List of search result element nodes
 */
async function waitForSearchResults(dbg, expectedResults) {
  if (expectedResults) {
    info(`Waiting for ${expectedResults} project search results`);
    await waitUntil(
      () =>
        findAllElements(dbg, "projectSearchFileResults").length ==
        expectedResults
    );
  } else {
    // If no results are expected, wait for the "no results" message to be displayed.
    info("Wait for project search to complete with no results");
    await waitUntil(() => {
      const projectSearchResult = findElementWithSelector(
        dbg,
        ".no-result-msg"
      );
      return projectSearchResult
        ? projectSearchResult.textContent ==
            DEBUGGER_L10N.getStr("projectTextSearch.noResults")
        : false;
    });
  }
  return findAllElements(dbg, "projectSearchFileResults");
}

/**
 * Get the no of expanded search results
 *
 * @param {Object} dbg
 * @return {Number} No of expanded results
 */
function getExpandedResultsCount(dbg) {
  return findAllElements(dbg, "projectSearchExpandedResults").length;
}

// This module is also loaded for Browser Toolbox tests, within the browser toolbox process
// which doesn't contain mochitests resource://testing-common URL.
// This isn't important to allow rejections in the context of the browser toolbox tests.
const protocolHandler = Services.io
  .getProtocolHandler("resource")
  .QueryInterface(Ci.nsIResProtocolHandler);
if (protocolHandler.hasSubstitution("testing-common")) {
  const { PromiseTestUtils } = ChromeUtils.importESModule(
    "resource://testing-common/PromiseTestUtils.sys.mjs"
  );
  PromiseTestUtils.allowMatchingRejectionsGlobally(/Connection closed/);
  this.PromiseTestUtils = PromiseTestUtils;

  // Debugger operations that are canceled because they were rendered obsolete by
  // a navigation or pause/resume end up as uncaught rejections. These never
  // indicate errors and are allowed in all debugger tests.
  // All the following are related to context middleware throwing on obsolete async actions:
  PromiseTestUtils.allowMatchingRejectionsGlobally(/DebuggerContextError/);
}

/**
 * Selects the specific black box context menu item
 * @param {Object} dbg
 * @param {String} itemName
 *                  The name of the context menu item.
 */
async function selectBlackBoxContextMenuItem(dbg, itemName) {
  let wait = null;
  if (itemName == "blackbox-line" || itemName == "blackbox-lines") {
    wait = Promise.any([
      waitForDispatch(dbg.store, "BLACKBOX_SOURCE_RANGES"),
      waitForDispatch(dbg.store, "UNBLACKBOX_SOURCE_RANGES"),
    ]);
  } else if (itemName == "blackbox") {
    wait = Promise.any([
      waitForDispatch(dbg.store, "BLACKBOX_WHOLE_SOURCES"),
      waitForDispatch(dbg.store, "UNBLACKBOX_WHOLE_SOURCES"),
    ]);
  }

  info(`Select the ${itemName} context menu item`);
  selectDebuggerContextMenuItem(dbg, `#node-menu-${itemName}`);
  return wait;
}

function openOutlinePanel(dbg, waitForOutlineList = true) {
  info("Select the outline panel");
  const outlineTab = findElementWithSelector(dbg, ".outline-tab a");
  EventUtils.synthesizeMouseAtCenter(outlineTab, {}, outlineTab.ownerGlobal);

  if (!waitForOutlineList) {
    return Promise.resolve();
  }

  return waitForElementWithSelector(dbg, ".outline-list");
}

// Test empty panel when source has not function or class symbols
// Test that anonymous functions do not show in the outline panel
function assertOutlineItems(dbg, expectedItems) {
  const outlineItems = Array.from(
    findAllElementsWithSelector(
      dbg,
      ".outline-list h2, .outline-list .outline-list__element"
    )
  );
  SimpleTest.isDeeply(
    outlineItems.map(i => i.innerText.trim()),
    expectedItems,
    "The expected items are displayed in the outline panel"
  );
}

async function checkAdditionalThreadCount(dbg, count) {
  await waitForState(
    dbg,
    () => {
      return dbg.selectors.getThreads().length == count;
    },
    "Have the expected number of additional threads"
  );
  ok(true, `Have ${count} threads`);
}

/**
 * Retrieve the text displayed as warning under the editor.
 */
function findFooterNotificationMessage(dbg) {
  return findElement(dbg, "editorNotificationFooter")?.innerText;
}

/**
 * Toggle a JavaScript Tracer settings via the toolbox toolbar button's context menu.
 *
 * @param {Object} dbg
 * @param {String} selector
 *        Selector for the menu item of the settings defined in devtools/client/framework/definitions.js.
 */
async function toggleJsTracerMenuItem(dbg, selector) {
  const button = dbg.toolbox.doc.getElementById("command-button-jstracer");
  EventUtils.synthesizeMouseAtCenter(
    button,
    { type: "contextmenu" },
    dbg.toolbox.win
  );
  const popup = await waitForContextMenu(dbg);
  const onHidden = BrowserTestUtils.waitForEvent(popup, "popuphidden");
  selectDebuggerContextMenuItem(dbg, selector);
  await onHidden;
}
