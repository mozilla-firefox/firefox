/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Validates that all ungrouped/standalone tabs have an explicit `aria-level="1"`
// while all tabs inside of tab groups have an explicit `aria-level="2"`. These
// attributes help users to understand the hierarchy of the tab strip, which we
// mainly communicate to users via visual outlines on the tab strip.

function forgetSavedTabGroups() {
  const tabGroups = SessionStore.getSavedTabGroups();
  tabGroups.forEach(tabGroup => SessionStore.forgetSavedTabGroup(tabGroup.id));
}

add_task(async function test_ARIA_level_on_tabs() {
  const tabs = await Promise.all(
    Array.from({ length: 8 }).map((_, index) =>
      addTab(`http://mochi.test:8888/${index}`)
    )
  );

  const group1Tabs = tabs.slice(1, 3);
  const group1 = gBrowser.addTabGroup(group1Tabs, {
    insertBefore: group1Tabs[0],
  });
  await BrowserTestUtils.waitForEvent(group1, "TabGrouped");

  const group2Tabs = tabs.slice(6);
  const group2 = gBrowser.addTabGroup(group2Tabs, {
    insertBefore: group2Tabs[0],
  });
  await BrowserTestUtils.waitForEvent(group2, "TabGrouped");

  Assert.ok(
    gBrowser.tabs
      .filter(tab => !tab.group)
      .every(tab => tab.getAttribute("aria-level") == "1"),
    "all tabs outside of tab groups have aria-level='1'"
  );

  Assert.ok(
    gBrowser.tabs
      .filter(tab => tab.group)
      .every(tab => tab.getAttribute("aria-level") == "2"),
    "all tabs inside of tab groups have aria-level='2'"
  );

  for (const tab of tabs) {
    BrowserTestUtils.removeTab(tab);
  }
  forgetSavedTabGroups();
});
