/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);
const { showBookmarksSidebar } = SidebarTestUtils.bookmarks;

const TEST_URL = "https://example.org/";
const TEST_TITLE = "Example Page";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.updatedBookmarks.enabled", true]],
  });
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    url: TEST_URL,
    title: TEST_TITLE,
  });
  await PlacesTestUtils.addVisits(TEST_URL);

  registerCleanupFunction(async () => {
    await PlacesUtils.bookmarks.eraseEverything();
    await PlacesUtils.history.clear();
  });
});

add_task(async function test_click() {
  const TEST_CASES = [
    {
      expected: "current",
    },
    {
      loadUILinkInBackground: true,
      expected: "current",
    },
    {
      loadUILinkInTab: true,
      expected: "foreground",
    },
    {
      loadUILinkInTab: true,
      loadUILinkInBackground: true,
      expected: "background",
    },
    {
      loadUILinkInTab: true,
      blankTab: true,
      expected: "current",
    },
  ];

  for (let target of ["bookmark", "history"]) {
    for (let {
      loadUILinkInTab = false,
      loadUILinkInBackground = false,
      blankTab = false,
      expected,
    } of TEST_CASES) {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.tabs.loadUILinkInTab", loadUILinkInTab],
          ["browser.tabs.loadUILinkInBackground", loadUILinkInBackground],
        ],
      });

      info(
        "Testing " +
          JSON.stringify({
            target,
            loadUILinkInTab,
            loadUILinkInBackground,
            blankTab,
            expected,
          })
      );
      await BrowserTestUtils.withNewTab(
        blankTab ? "about:blank" : "about:robots",
        async browser => {
          let tab = gBrowser.getTabForBrowser(browser);
          let tabCount = gBrowser.tabs.length;
          let onOpened =
            expected == "current"
              ? BrowserTestUtils.browserLoaded(browser, false, TEST_URL)
              : BrowserTestUtils.waitForNewTab(gBrowser, TEST_URL);

          if (target == "bookmark") {
            await clickOnBookmark();
          } else {
            await clickOnHistory();
          }

          let newTab = await onOpened;
          if (expected == "current") {
            Assert.equal(gBrowser.tabs.length, tabCount);
            Assert.equal(gBrowser.selectedTab, tab);
          } else {
            Assert.equal(gBrowser.tabs.length, tabCount + 1);
            Assert.equal(
              gBrowser.selectedTab == newTab,
              expected == "foreground"
            );
            BrowserTestUtils.removeTab(newTab);
          }
        }
      );

      await SpecialPowers.popPrefEnv();
    }
  }
});

async function clickOnBookmark() {
  const { component, contentWindow } = await showBookmarksSidebar(window);
  const innerList = await expandToolbarFolder(component.bookmarkList);
  let row;
  await TestUtils.waitForCondition(() => {
    row = [...innerList.rowEls].find(({ title }) => title === TEST_TITLE);
    return row;
  }, "Waiting for the bookmark row");
  AccessibilityUtils.setEnv({ focusableRule: false });
  EventUtils.synthesizeMouseAtCenter(row, {}, contentWindow);
  AccessibilityUtils.resetEnv();
}

async function clickOnHistory() {
  const { component, contentWindow } = await showHistorySidebar();
  let row;
  await TestUtils.waitForCondition(() => {
    row = [...component.lists]
      .flatMap(list => [...list.rowEls])
      .find(({ url }) => url == TEST_URL);
    return row;
  }, "Waiting for the history row");
  AccessibilityUtils.setEnv({ focusableRule: false });
  EventUtils.synthesizeMouseAtCenter(row.mainEl, {}, contentWindow);
  AccessibilityUtils.resetEnv();
}
