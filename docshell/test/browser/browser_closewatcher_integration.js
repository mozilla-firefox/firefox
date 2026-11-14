/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_PAGE =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.com"
  ) + "dummy_page.html";
const CLOSEWATCHER_PAGE =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.com"
  ) + "page_with_closewatcher.html";

const runTest =
  (bool, baseURL = TEST_PAGE) =>
  async () => {
    let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, baseURL);

    await new Promise(resolve =>
      SessionStore.getSessionHistory(gBrowser.selectedTab, resolve)
    );

    // Assert the hasActiveCloseWatcher property
    is(
      gBrowser.selectedBrowser.hasActiveCloseWatcher,
      bool,
      `hasActiveCloseWatcher is ${bool}`
    );

    gBrowser.selectedBrowser.processCloseRequest();

    // CloseWatcher may not be immediately closed as the request is over IPC, so allow some grace
    // by checking every 100ms to see if hasActiveCloseWatcher flips to false.
    {
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      const sleep = ms => new Promise(r => setTimeout(r, ms));
      const hasActiveCloseWatcherEventuallyFalse = (async () => {
        while (gBrowser.selectedBrowser.hasActiveCloseWatcher) {
          await sleep(50);
        }
      })();
      await Promise.race([hasActiveCloseWatcherEventuallyFalse, sleep(3000)]);
    }

    // Assert the hasActiveCloseWatcher property is false after a close request
    is(
      gBrowser.selectedBrowser.hasActiveCloseWatcher,
      false,
      `hasActiveCloseWatcher is false after processCloseRequest`
    );

    BrowserTestUtils.removeTab(tab);
  };

add_task(runTest(false, TEST_PAGE));
add_task(runTest(true, CLOSEWATCHER_PAGE));

add_task(async function test_processCloseRequest_unfocused_tab() {
  let cwTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    CLOSEWATCHER_PAGE
  );
  let cwBrowser = cwTab.linkedBrowser;

  await new Promise(resolve => SessionStore.getSessionHistory(cwTab, resolve));

  is(
    cwBrowser.hasActiveCloseWatcher,
    true,
    "CloseWatcher tab reports an active close watcher"
  );

  // Open a second tab, which moves focus away from the CloseWatcher tab.
  let otherTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    TEST_PAGE
  );
  isnot(
    gBrowser.selectedTab,
    cwTab,
    "CloseWatcher tab is no longer the selected tab"
  );

  // Send processCloseRequest to the now-unfocused CloseWatcher tab.
  cwBrowser.processCloseRequest();

  // SpecialPowers.spawn performs an IPC roundtrip to the content process,
  // which is ordered after the ProcessCloseRequest message. When it resolves
  // we know the handler has already run.
  await SpecialPowers.spawn(cwBrowser, [], () => {});

  // The close watcher should still be active since the tab was not focused
  // when the request was processed.
  is(
    cwBrowser.hasActiveCloseWatcher,
    true,
    "CloseWatcher is still active because tab was not focused"
  );

  BrowserTestUtils.removeTab(otherTab);
  BrowserTestUtils.removeTab(cwTab);
});
