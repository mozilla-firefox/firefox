/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

// Move a tab to a new window the reload it.  In Bug 1691135 it would not
// reload.
add_task(async function test() {
  let tab1 = await addTab();
  let tab2 = await addTab();

  is(gBrowser.multiSelectedTabsCount, 0, "Zero multiselected tabs");

  await BrowserTestUtils.switchTab(gBrowser, tab1);
  let prevBrowser = tab1.linkedBrowser;

  let delayedStartupPromise = BrowserTestUtils.waitForNewWindow();
  let newWindow = gBrowser.replaceTabsWithWindow(tab1);
  await delayedStartupPromise;

  ok(
    !prevBrowser.frameLoader,
    "the swapped-from browser's frameloader has been destroyed"
  );

  let gBrowser2 = newWindow.gBrowser;

  is(gBrowser.visibleTabs.length, 2, "Two tabs now in the old window");
  is(gBrowser2.visibleTabs.length, 1, "One tabs in the new window");

  tab1 = gBrowser2.visibleTabs[0];
  ok(tab1, "Got a tab1");
  await tab1.focus();

  await TabStateFlusher.flush(tab1.linkedBrowser);

  info("Reloading");
  let tab1Loaded = BrowserTestUtils.browserLoaded(
    gBrowser2.getBrowserForTab(tab1)
  );

  gBrowser2.reload();
  ok(await tab1Loaded, "Tab reloaded");

  await BrowserTestUtils.closeWindow(newWindow);
  await BrowserTestUtils.removeTab(tab2);
});
