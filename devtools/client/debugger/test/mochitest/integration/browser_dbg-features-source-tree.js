/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

/**
 * This test focuses on the SourceTree component, where we display all debuggable sources.
 *
 * The first two tests expand the tree via manual DOM events (first with clicks and second with keys).
 * `waitForSourcesInSourceTree()` is a key assertion method. Passing `{noExpand: true}`
 * is important to avoid automatically expand the source tree.
 *
 * The following tests depend on auto-expand and only assert all the sources possibly displayed
 */

"use strict";

const testServer = createVersionizedHttpTestServer(
  "../examples/sourcemaps-reload-uncompressed"
);
const TEST_URL = testServer.urlFor("index.html");

/**
 * This test opens the SourceTree manually via click events on the nested source,
 * and then adds a source dynamically and asserts it is visible.
 */
add_task(async function testSimpleSourcesWithManualClickExpand() {
  const dbg = await initDebugger(
    "doc-sources.html",
    "simple1.js",
    "simple2.js",
    "nested-source.js",
    "long.js"
  );

  // Expand nodes and make sure more sources appear.
  is(
    getSourceTreeLabel(dbg, 1),
    "Main Thread",
    "Main thread is labeled properly"
  );
  info("Before interacting with the source tree, no source are displayed");
  await waitForSourcesInSourceTree(dbg, [], { noExpand: true });
  await clickElement(dbg, "sourceDirectoryLabel", 3);
  info(
    "After clicking on the directory, all sources but the nested ones are displayed"
  );
  await waitForSourcesInSourceTree(
    dbg,
    ["doc-sources.html", "simple1.js", "simple2.js", "long.js"],
    { noExpand: true }
  );

  await clickElement(dbg, "sourceDirectoryLabel", 4);
  info(
    "After clicking on the nested directory, the nested source is also displayed"
  );
  await waitForSourcesInSourceTree(
    dbg,
    [
      "doc-sources.html",
      "simple1.js",
      "simple2.js",
      "long.js",
      "nested-source.js",
    ],
    { noExpand: true }
  );

  const selected = waitForDispatch(dbg.store, "SET_SELECTED_LOCATION");
  await clickElement(dbg, "sourceNode", 5);
  await selected;
  await waitForSelectedSource(dbg, "nested-source.js");

  // Ensure the source file clicked is now focused
  await waitForElementWithSelector(dbg, ".sources-list .focused");

  const selectedSource = dbg.selectors.getSelectedSource().url;
  ok(selectedSource.includes("nested-source.js"), "nested-source is selected");
  await assertNodeIsFocused(dbg, 5);

  // Make sure new sources appear in the list.
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], function () {
    const script = content.document.createElement("script");
    script.src = "math.min.js";
    content.document.body.appendChild(script);
  });

  info("After adding math.min.js, we got a new source displayed");
  await waitForSourcesInSourceTree(
    dbg,
    [
      "doc-sources.html",
      "simple1.js",
      "simple2.js",
      "long.js",
      "nested-source.js",
      "math.min.js",
    ],
    { noExpand: true }
  );
  is(
    getSourceNodeLabel(dbg, 8),
    "math.min.js",
    "math.min.js - The dynamic script exists"
  );

  info("Assert that nested-source.js is still the selected source");
  await assertNodeIsFocused(dbg, 5);

  info("Test the copy to clipboard context menu");
  const mathMinTreeNode = findSourceNodeWithText(dbg, "math.min.js");
  await triggerSourceTreeContextMenu(
    dbg,
    mathMinTreeNode,
    "#node-menu-copy-source"
  );
  const clipboardData = SpecialPowers.getClipboardData("text/plain");
  is(
    clipboardData,
    EXAMPLE_URL + "math.min.js",
    "The clipboard content is the selected source URL"
  );

  info("Test the download file context menu");
  // Before trigerring the menu, mock the file picker
  const MockFilePicker = SpecialPowers.MockFilePicker;
  MockFilePicker.init();
  const nsiFile = new FileUtils.File(
    PathUtils.join(PathUtils.tempDir, `export_source_content_${Date.now()}.log`)
  );
  MockFilePicker.setFiles([nsiFile]);
  const path = nsiFile.path;

  await triggerSourceTreeContextMenu(
    dbg,
    mathMinTreeNode,
    "#node-menu-download-file"
  );

  info("Wait for the downloaded file to be fully saved to disk");
  await TestUtils.waitForCondition(() => IOUtils.exists(path));
  await TestUtils.waitForCondition(async () => {
    const { size } = await IOUtils.stat(path);
    return size > 0;
  });
  const buffer = await IOUtils.read(path);
  const savedFileContent = new TextDecoder().decode(buffer);

  const mathMinRequest = await fetch(EXAMPLE_URL + "math.min.js");
  const mathMinContent = await mathMinRequest.text();

  is(
    savedFileContent,
    mathMinContent,
    "The downloaded file has the expected content"
  );

  dbg.toolbox.closeToolbox();
});

/**
 * Test keyboard arrow behaviour on the SourceTree with a nested folder
 * that we manually expand/collapse via arrow keys.
 */
add_task(async function testSimpleSourcesWithManualKeyShortcutsExpand() {
  const dbg = await initDebugger(
    "doc-sources.html",
    "simple1.js",
    "simple2.js",
    "nested-source.js",
    "long.js"
  );

  // Before clicking on the source label, no source is displayed
  await waitForSourcesInSourceTree(dbg, [], { noExpand: true });
  await clickElement(dbg, "sourceDirectoryLabel", 3);
  // Right after, all sources, but the nested one are displayed
  await waitForSourcesInSourceTree(
    dbg,
    ["doc-sources.html", "simple1.js", "simple2.js", "long.js"],
    { noExpand: true }
  );

  // The directory label was clicked, so it is focused
  await assertNodeIsFocused(dbg, 3);

  // Right key on an open dir focuses its first child, the nested dir
  await pressKey(dbg, "Right");
  await assertNodeIsFocused(dbg, 4);

  // Right key on a closed dir expands it and displays the nested source
  await pressKey(dbg, "Right");
  await waitForSourcesInSourceTree(
    dbg,
    [
      "doc-sources.html",
      "simple1.js",
      "simple2.js",
      "long.js",
      "nested-source.js",
    ],
    { noExpand: true }
  );
  await assertNodeIsFocused(dbg, 4);

  // Right key on an open dir focuses its first child, the nested source
  await pressKey(dbg, "Right");
  await assertNodeIsFocused(dbg, 5);

  // Right key on a source focuses the next node
  await pressKey(dbg, "Right");
  await assertNodeIsFocused(dbg, 6);

  // Down key on a source
  await pressKey(dbg, "Down");
  await assertNodeIsFocused(dbg, 7);

  // Go to bottom of tree and press down key
  await pressKey(dbg, "Down");
  await assertNodeIsFocused(dbg, 8);
  await pressKey(dbg, "Down");
  await assertNodeIsFocused(dbg, 9);
  await pressKey(dbg, "Down");
  await assertNodeIsFocused(dbg, 9);

  // Up key on a source
  await pressKey(dbg, "Up");
  await assertNodeIsFocused(dbg, 8);

  // Left key on a source focuses its parent dir
  await pressKey(dbg, "Left");
  await assertNodeIsFocused(dbg, 3);

  // Down key on a dir focuses the next (open) dir
  await pressKey(dbg, "Down");
  await assertNodeIsFocused(dbg, 4);

  // Left key on an open dir collapses it and hides the nested source
  await pressKey(dbg, "Left");
  await waitForSourcesInSourceTree(
    dbg,
    ["doc-sources.html", "simple1.js", "simple2.js", "long.js"],
    { noExpand: true }
  );
  await assertNodeIsFocused(dbg, 4);

  // Left key on a closed dir focuses its parent dir
  await pressKey(dbg, "Left");
  await assertNodeIsFocused(dbg, 3);

  // Go to top of tree and press up key
  await pressKey(dbg, "Up");
  await assertNodeIsFocused(dbg, 2);
  await pressKey(dbg, "Up");
  await assertNodeIsFocused(dbg, 1);
  await pressKey(dbg, "Up");
  await assertNodeIsFocused(dbg, 1);
  dbg.toolbox.closeToolbox();
});

/**
 * Tests that the source tree works with all the various types of sources
 * coming from the integration test page.
 *
 * Also assert a few extra things on sources with query strings:
 *  - they can be pretty printed,
 *  - quick open matches them,
 *  - you can set breakpoint on them.
 */
add_task(async function testSourceTreeOnTheIntegrationTestPage() {
  // We open against a blank page and only then navigate to the test page
  // so that sources aren't GC-ed before opening the debugger.
  // When we (re)load a page while the debugger is opened, the debugger
  // will force all sources to be held in memory.
  const dbg = await initDebuggerWithAbsoluteURL("about:blank");

  await navigateToAbsoluteURL(
    dbg,
    TEST_URL,
    "index.html",
    "script.js",
    "test-functions.js",
    "query.js?x=1",
    "query.js?x=2",
    "query2.js?y=3",
    "bundle.js",
    "original.js",
    "replaced-bundle.js",
    "removed-original.js",
    "named-eval.js"
  );

  info("Verify source tree content");
  await waitForSourcesInSourceTree(dbg, INTEGRATION_TEST_PAGE_SOURCES);

  info("Verify Thread Source Items");
  const mainThreadItem = findSourceTreeThreadByName(dbg, "Main Thread");
  ok(mainThreadItem, "Found the thread item for the main thread");
  ok(
    mainThreadItem.querySelector("span.dbg-img-window"),
    "The thread has the window icon"
  );

  info(
    "Assert the number of sources and source actors for the same-url.sjs sources"
  );
  const sameUrlSource = findSource(dbg, "same-url.sjs");
  ok(sameUrlSource, "Found same-url.js in the main thread");

  const sourceActors = dbg.selectors.getSourceActorsForSource(sameUrlSource.id);

  const mainThread = dbg.selectors
    .getAllThreads()
    .find(thread => thread.name == "Main Thread");

  const expectedSameUrlSources = 3;
  is(
    sourceActors.filter(actor => actor.thread == mainThread.actor).length,
    expectedSameUrlSources,
    `same-url.js is loaded ${expectedSameUrlSources} times in the main thread`
  );

  const iframeThread = dbg.selectors
    .getAllThreads()
    .find(thread => thread.name == testServer.urlFor("iframe.html"));

  is(
    sourceActors.filter(actor => actor.thread == iframeThread.actor).length,
    1,
    "same-url.js is loaded one time in the iframe thread"
  );

  const workerThread = dbg.selectors
    .getAllThreads()
    .find(thread => thread.url == testServer.urlFor("same-url.sjs"));

  is(
    sourceActors.filter(actor => actor.thread == workerThread.actor).length,
    1,
    "same-url.js is loaded one time in the worker thread"
  );

  const workerThreadItem = findSourceTreeThreadByName(dbg, "same-url.sjs");
  ok(workerThreadItem, "Found the thread item for the worker");
  ok(
    workerThreadItem.querySelector("span.dbg-img-worker"),
    "The thread has the worker icon"
  );

  info("Verify source icons");
  assertSourceIcon(dbg, "index.html", "file");
  assertSourceIcon(dbg, "script.js", "javascript");
  assertSourceIcon(dbg, "query.js?x=1", "javascript");
  assertSourceIcon(dbg, "original.js", "javascript");
  // Framework icons are only displayed when we parse the source,
  // which happens when we select the source
  assertSourceIcon(dbg, "react-component-module.js", "javascript");
  const onResumed = SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    function () {
      content.eval("pauseInReact()");
    }
  );
  await waitForPaused(dbg);
  assertSourceIcon(dbg, "react-component-module.js", "javascript");
  await resume(dbg);
  await onResumed;

  info("Select an original source while bundle should be opened by default");
  // The setting should be ignored when selecting a source from the source tree
  await dbg.actions.setDefaultSelectedLocation(false);
  await selectSourceFromSourceTree(dbg, "original.js");
  assertTextContentOnLine(dbg, 1, `window.bar = function bar() {`);

  info("Verify blackbox source icon");
  await selectSourceFromSourceTree(dbg, "script.js");
  await clickElement(dbg, "blackbox");
  await waitForDispatch(dbg.store, "BLACKBOX_WHOLE_SOURCES");
  assertSourceIcon(dbg, "script.js", "blackBox");
  await clickElement(dbg, "blackbox");
  await waitForDispatch(dbg.store, "UNBLACKBOX_WHOLE_SOURCES");
  assertSourceIcon(dbg, "script.js", "javascript");

  info("Assert the content of the named eval");
  await selectSourceFromSourceTree(dbg, "named-eval.js");
  assertTextContentOnLine(dbg, 3, `console.log("named-eval");`);

  info("Assert that nameless eval don't show up in the source tree");
  invokeInTab("breakInEval");
  await waitForPaused(dbg);
  await waitForSourcesInSourceTree(dbg, INTEGRATION_TEST_PAGE_SOURCES);
  await resume(dbg);

  info("Assert the content of sources with query string");
  await selectSourceFromSourceTree(dbg, "query.js?x=1");
  const tab = findElement(dbg, "activeTab");
  is(tab.innerText, "query.js?x=1", "Tab label is query.js?x=1");
  assertTextContentOnLine(
    dbg,
    1,
    `function query() {console.log("query x=1");}`
  );
  await addBreakpoint(dbg, "query.js?x=1", 1);
  assertBreakpointHeading(dbg, "query.js?x=1", 0);

  // pretty print the source and check the tab text
  await togglePrettyPrint(dbg);

  const prettyTab = findElement(dbg, "activeTab");
  is(prettyTab.innerText, "query.js?x=1", "Tab label is query.js?x=1");
  assertBreakpointHeading(dbg, "query.js?x=1", 0);
  assertTextContentOnLine(dbg, 1, `function query() {`);
  // Note the replacements of " by ' here:
  assertTextContentOnLine(dbg, 2, `console.log("query x=1");`);

  // assert quick open works with queries
  pressKey(dbg, "quickOpen");
  type(dbg, "query.js?x");

  // There can be intermediate updates in the results,
  // so wait for the final expected value
  await waitFor(async () => {
    const resultItem = findElement(dbg, "resultItems");
    if (!resultItem) {
      return false;
    }
    return resultItem.innerText.includes("query.js?x=1");
  }, "Results include the source with the query string");
  dbg.toolbox.closeToolbox();
});

/**
 * Verify that Web Extension content scripts appear only when
 * devtools.chrome.enabled is set to true and that they get
 * automatically re-selected on page reload.
 */
add_task(async function testSourceTreeWithWebExtensionContentScript() {
  const extension = await installAndStartContentScriptExtension();

  // Ensure that the setting to show content script is off before running the test
  await pushPref("devtools.debugger.show-content-scripts", false);
  let dbg = await initDebugger("doc-content-script-sources.html");
  // Let some time for unexpected source to appear
  await wait(1000);
  // There is no content script, but still html pages inline sources
  await waitForSourcesInSourceTree(dbg, [
    "doc-content-script-sources.html",
    "doc-strict.html",
  ]);
  await dbg.toolbox.closeToolbox();

  const toolbox = await openToolboxForTab(gBrowser.selectedTab, "jsdebugger");
  dbg = createDebuggerContext(toolbox);

  info("Enable the content script setting");
  await toggleSourcesTreeSettingsMenuItem(dbg, {
    className: ".debugger-settings-menu-item-show-content-scripts",
    isChecked: false,
  });

  await waitForSourcesInSourceTree(dbg, [
    "doc-content-script-sources.html",
    "doc-strict.html",
    "content_script.js",
  ]);
  await selectSourceFromSourceTree(dbg, "content_script.js");
  ok(
    findElementWithSelector(dbg, ".sources-list .focused"),
    "Source is focused"
  );

  // Note that the thread item also contains the extension name
  const contentScriptGroupItem = findSourceTreeGroupByName(
    dbg,
    "Test content script extension"
  );
  ok(contentScriptGroupItem, "Found the group item for the content script");
  ok(
    contentScriptGroupItem.querySelector("span.dbg-img-extension"),
    "The group has the extension icon"
  );
  assertSourceIcon(dbg, "content_script.js", "javascript");

  for (let i = 1; i < 3; i++) {
    info(
      `Reloading tab (${i} time), the content script should always be reselected`
    );
    gBrowser.reloadTab(gBrowser.selectedTab);
    await waitForSelectedSource(dbg, "content_script.js");
    ok(
      findElementWithSelector(dbg, ".sources-list .focused"),
      "Source is focused"
    );
  }
  await dbg.toolbox.closeToolbox();

  await extension.unload();
});

add_task(async function testSourceTreeWithEncodedPaths() {
  const httpServer = createTestHTTPServer();
  httpServer.registerContentType("html", "text/html");
  httpServer.registerContentType("js", "application/javascript");

  httpServer.registerPathHandler("/index.html", function (request, response) {
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.write(`<!DOCTYPE html>
    <html>
      <head>
      <script src="/my folder/my file.js"></script>
      <script src="/malformedUri.js?%"></script>
      </head>
      <body>
      <h1>Encoded scripts paths</h1>
      </body>
    `);
  });
  httpServer.registerPathHandler(
    encodeURI("/my folder/my file.js"),
    function (request, response) {
      response.setStatusLine(request.httpVersion, 200, "OK");
      response.setHeader("Content-Type", "application/javascript", false);
      response.write(`const x = 42`);
    }
  );
  httpServer.registerPathHandler(
    "/malformedUri.js",
    function (request, response) {
      response.setStatusLine(request.httpVersion, 200, "OK");
      response.setHeader("Content-Type", "application/javascript", false);
      response.write(`const y = "malformed"`);
    }
  );
  const port = httpServer.identity.primaryPort;

  const dbg = await initDebuggerWithAbsoluteURL(
    `http://localhost:${port}/index.html`,
    "my file.js"
  );

  await waitForSourcesInSourceTree(dbg, ["my file.js", "malformedUri.js?%"]);
  ok(
    true,
    "source name are decoded in the tree, and malformed uri source are displayed"
  );
  is(
    // We don't have any specific class on the folder item, so let's target the folder
    // icon next sibling, which is the directory label.
    findElementWithSelector(
      dbg,
      ".sources-panel .node .dbg-img-folder + .label"
    ).innerText,
    "my folder",
    "folder name is decoded in the tree"
  );
});

/**
 * Assert the location displayed in the breakpoint list, in the right sidebar.
 *
 * @param {object} dbg
 * @param {string} label
 *        The expected displayed location
 * @param {number} index
 *        The position of the breakpoint in the list to verify
 */
function assertBreakpointHeading(dbg, label, index) {
  const breakpointHeading = findAllElements(dbg, "breakpointHeadings")[index]
    .innerText;
  is(breakpointHeading, label, `Breakpoint heading is ${label}`);
}
