/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests that selecting a vertical tab outside of the visible part of the tab
 * strip scrolls it clear of the overflow fade, which is one tab row long.
 */

add_setup(() =>
  SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", true],
      ["sidebar.verticalTabs", true],
    ],
  })
);

add_task(async function () {
  let win = await BrowserTestUtils.openNewBrowserWindow();
  let { gBrowser } = win;
  let { arrowScrollbox } = gBrowser.tabContainer;
  let { scrollbox } = arrowScrollbox;

  await BrowserTestUtils.overflowTabs(null, win, {
    overflowAtStart: false,
    overflowTabFactor: 3,
  });
  Assert.ok(arrowScrollbox.overflowing, "Tab strip should be overflowing");

  let tabs = gBrowser.visibleTabs;
  let rect = element => element.getBoundingClientRect();
  let fadeLength = Math.min(
    rect(tabs[2]).top - rect(tabs[1]).top,
    scrollbox.clientHeight / 4
  );

  async function selectAndWaitForScroll(tab) {
    gBrowser.selectedTab = tab;
    await TestUtils.waitForCondition(
      () =>
        rect(tab).top >= rect(scrollbox).top &&
        rect(tab).bottom <= rect(scrollbox).bottom,
      "Selected tab is scrolled into view"
    );
    await new Promise(resolve => win.requestAnimationFrame(resolve));
  }

  scrollbox.scrollTo({ top: 0, behavior: "instant" });
  let tabBelow = tabs.find(tab => rect(tab).top >= rect(scrollbox).bottom);
  await selectAndWaitForScroll(tabBelow);
  Assert.greaterOrEqual(
    rect(scrollbox).bottom - rect(tabBelow).bottom,
    fadeLength - 1,
    "Tab scrolled into view from below is clear of the bottom fade"
  );

  scrollbox.scrollTo({ top: scrollbox.scrollTopMax, behavior: "instant" });
  let tabAbove = tabs.findLast(tab => rect(tab).bottom <= rect(scrollbox).top);
  await selectAndWaitForScroll(tabAbove);
  Assert.greaterOrEqual(
    rect(tabAbove).top - rect(scrollbox).top,
    fadeLength - 1,
    "Tab scrolled into view from above is clear of the top fade"
  );

  await BrowserTestUtils.closeWindow(win);
});
