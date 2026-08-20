/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TabState } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabState.sys.mjs"
);

/**
 * Simulate a restart of a tab by removing it, then add a lazy tab
 * which is restored with the tabData of the removed tab.
 *
 * @param tab
 *        The tab to restart.
 * @return {object} the restored lazy tab
 */
const restartTab = async function (tab) {
  let tabData = TabState.clone(tab);
  BrowserTestUtils.removeTab(tab);

  let restoredLazyTab = BrowserTestUtils.addTab(gBrowser, "", {
    createLazyBrowser: true,
  });
  SessionStore.setTabState(restoredLazyTab, JSON.stringify(tabData));
  return restoredLazyTab;
};

function get_tab_state(tab) {
  return JSON.parse(SessionStore.getTabState(tab));
}

add_task(async function () {
  const tab = BrowserTestUtils.addTab(gBrowser, "http://mochi.test:8888/");
  const browser = gBrowser.getBrowserForTab(tab);
  await BrowserTestUtils.browserLoaded(browser);

  // Let's make sure the tab is not in a muted state at the beginning
  ok(!("muted" in get_tab_state(tab)), "Tab should not be in a muted state");

  info("toggling Muted audio...");
  tab.toggleMuteAudio();

  ok("muted" in get_tab_state(tab), "Tab should be in a muted state");

  info("Restarting tab...");
  let restartedTab = await restartTab(tab);

  ok(
    "muted" in get_tab_state(restartedTab),
    "Restored tab should still be in a muted state after restart"
  );
  ok(!restartedTab.linkedPanel, "Restored tab should not be inserted");

  BrowserTestUtils.removeTab(restartedTab);
});
