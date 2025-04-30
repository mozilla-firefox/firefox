/* Any copyright is dedicated to the Public Domain.
  https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// 1. Validates that all ungrouped/standalone tabs do not set `aria-posinset` nor
// `aria-setsize`, since the default rule for `tab`s in a `role="tablist"` is
// to count all tabs as being in a single set.
// 2. Validates that all tabs inside of tab groups have an explicit `aria-setsize`
// based on the number of tabs in its tab group and an explicit `aria-posinset` as
// a 1-based index of the tab within the tab group. This helps give a11y tools more
// context to tell a user where they are within a tab group, not just where
// they are within the entire tab strip.

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

  const group1Tabs = tabs.slice(1, 2);
  const group1 = gBrowser.addTabGroup(group1Tabs, {
    insertBefore: group1Tabs[0],
  });
  await BrowserTestUtils.waitForEvent(group1, "TabGrouped");

  const group2Tabs = tabs.slice(4);
  const group2 = gBrowser.addTabGroup(group2Tabs, {
    insertBefore: group2Tabs[0],
  });
  await BrowserTestUtils.waitForEvent(group2, "TabGrouped");

  Assert.ok(
    gBrowser.tabs
      .filter(tab => !tab.group)
      .every(tab => !tab.hasAttribute("aria-setsize")),
    "all tabs outside of tab groups should NOT set aria-setsize"
  );
  Assert.ok(
    gBrowser.tabs
      .filter(tab => !tab.group)
      .every(tab => !tab.hasAttribute("aria-posinset")),
    "all tabs outside of tab groups should NOT set aria-posinset"
  );

  Assert.ok(
    group1.tabs.every(
      (tab, index, array) =>
        tab.getAttribute("aria-setsize") == array.length &&
        tab.getAttribute("aria-posinset") == index + 1
    ),
    "all tabs inside of tab group 1 should set aria-setsize/aria-posinset"
  );
  Assert.ok(
    group2.tabs.every(
      (tab, index, array) =>
        tab.getAttribute("aria-setsize") == array.length &&
        tab.getAttribute("aria-posinset") == index + 1
    ),
    "all tabs inside of tab group 2 should set aria-setsize/aria-posinset"
  );

  for (const tab of tabs) {
    BrowserTestUtils.removeTab(tab);
  }
  forgetSavedTabGroups();
});
