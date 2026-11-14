/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

/**
 * This test focuses on the Tabs component, where we display all opened sources.
 */

"use strict";

const testServer = createVersionizedHttpTestServer(
  "../examples/sourcemaps-reload-uncompressed"
);
const TEST_URL = testServer.urlFor("index.html");

add_task(async function () {
  // We open against a blank page and only then navigate to the test page
  // so that sources aren't GC-ed before opening the debugger.
  // When we (re)load a page while the debugger is opened, the debugger
  // will force all sources to be held in memory.
  const dbg = await initDebuggerWithAbsoluteURL("about:blank");

  await navigateToAbsoluteURL(dbg, TEST_URL, ...INTEGRATION_TEST_PAGE_SOURCES);

  info("Wait for all sources to be displayed in the source tree");
  let displayedSources;
  await waitFor(() => {
    displayedSources = dbg.selectors.getDisplayedSourcesList();
    return displayedSources.length == INTEGRATION_TEST_PAGE_SOURCES.length;
  }, "Got the expected number of sources from the selectors");

  // Open each visible source in tabs
  const uniqueUrls = new Set();
  for (const source of displayedSources) {
    info(`Opening '${source.url}'`);
    await selectSource(dbg, source);
    uniqueUrls.add(source.url);
  }

  // Some sources are loaded from the same URL and only one tab will be opened for them
  is(countTabs(dbg), uniqueUrls.size, "Got a tab for each distinct source URL");

  invokeInTab("breakInEval");
  await waitFor(
    () => countTabs(dbg) == uniqueUrls.size + 1,
    "Wait for the tab for the new evaled source"
  );
  await resume(dbg);

  invokeInTab("breakInEval");
  await waitFor(
    () => countTabs(dbg) == uniqueUrls.size + 2,
    "Wait for yet another tab for the second evaled source"
  );
  await resume(dbg);

  await reload(dbg, ...INTEGRATION_TEST_PAGE_SOURCES);

  await waitFor(
    () => countTabs(dbg) == uniqueUrls.size,
    "Wait for tab count to be fully restored"
  );

  is(
    countTabs(dbg),
    uniqueUrls.size,
    "Still get the same number of tabs after reload"
  );

  await selectSource(dbg, "query.js?x=1");
  // Ensure focusing the window, otherwise copyToTheClipboard doesn't emit "copy" event.
  dbg.panel.panelWin.focus();

  info("Open the current active tab context menu");
  const waitForOpen = waitForContextMenu(dbg);
  rightClickElement(dbg, "activeTab");
  await waitForOpen;
  info("Trigger copy to source context menu");
  await waitForClipboardPromise(
    () => selectContextMenuItem(dbg, `#node-menu-copy-source`),
    `function query() {console.log("query x=1");}\n`
  );

  // Update displayed sources, so that we pass the right source objects to closeTab
  displayedSources = dbg.selectors.getDisplayedSourcesList();
  for (const source of displayedSources) {
    info(`Closing '${source.url}'`);
    await closeTab(dbg, source);
  }

  is(countTabs(dbg), 0, "All tabs are closed");
});
