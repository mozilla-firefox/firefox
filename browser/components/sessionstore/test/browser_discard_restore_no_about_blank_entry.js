/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_URL = "https://example.com/";

/**
 * Restoring a discarded tab that was created on about:blank must not add an
 * about:blank entry to its session history.
 */
add_task(async function test_restore_discarded_tab_history() {
  let tab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let browser = tab.linkedBrowser;
  BrowserTestUtils.startLoadingURIString(browser, TEST_URL);
  await BrowserTestUtils.browserLoaded(browser, false, TEST_URL);
  is(
    browser.browsingContext.sessionHistory.count,
    1,
    "One history entry before discarding"
  );
  await TabStateFlusher.flush(browser);

  isnot(gBrowser.selectedTab, tab, "The tab to discard is in the background");
  ok(
    gBrowser.discardBrowser(tab, { forceDiscard: true }),
    "The background tab was discarded"
  );
  ok(!tab.linkedPanel, "The discarded tab has a lazy browser");

  let restored = promiseTabRestored(tab);
  await BrowserTestUtils.switchTab(gBrowser, tab);
  await restored;

  let sessionHistory = tab.linkedBrowser.browsingContext.sessionHistory;
  is(sessionHistory.count, 1, "One history entry after restoring");
  is(
    sessionHistory.getEntryAtIndex(0).URI.spec,
    TEST_URL,
    "The entry is the restored page"
  );

  BrowserTestUtils.removeTab(tab);
});
