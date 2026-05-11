/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.splitView.enabled", true]],
  });
});

registerCleanupFunction(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.splitView.enabled", false]],
  });
});

/**
 * Synthesize a key press and wait for an element to be focused.
 *
 * @param {Element} element
 * @param {string} keyCode
 * @param {ChromeWindow} contentWindow
 */
async function focusWithKeyboard(element, keyCode, contentWindow) {
  await SimpleTest.promiseFocus(contentWindow);
  const focused = BrowserTestUtils.waitForEvent(
    element,
    "focus",
    contentWindow
  );
  EventUtils.synthesizeKey(keyCode, {}, contentWindow);
  await focused;
}

/**
 * @param {MozTabbrowserTab} tab
 * @param {function(splitViewMenuItem: Element, unsplitMenuItem: Element) => Promise<void>} callback
 */
const withTabMenu = async function (tab, callback) {
  const tabContextMenu = document.getElementById("tabContextMenu");
  Assert.equal(
    tabContextMenu.state,
    "closed",
    "context menu is initially closed"
  );
  const contextMenuShown = BrowserTestUtils.waitForPopupEvent(
    tabContextMenu,
    "shown"
  );

  EventUtils.synthesizeMouseAtCenter(
    tab,
    { type: "contextmenu", button: 2 },
    window
  );
  await contextMenuShown;

  const moveTabToNewSplitViewItem = document.getElementById(
    "context_moveTabToSplitView"
  );
  const unloadTabItem = document.getElementById("context_unloadTab");
  const unsplitTabItem = document.getElementById("context_separateSplitView");

  let contextMenuHidden = BrowserTestUtils.waitForPopupEvent(
    tabContextMenu,
    "hidden"
  );
  await callback(moveTabToNewSplitViewItem, unloadTabItem, unsplitTabItem);
  tabContextMenu.hidePopup();
  info("Hide popup");
  return await contextMenuHidden;
};

add_task(async function test_contextMenuMoveTabsToNewSplitView() {
  const tab1 = await addTab();
  const tab2 = await addTab();
  const tab3 = await addTab();

  // Click the first tab in our test split view to make sure the default tab at the
  // start of the tab strip is deselected
  EventUtils.synthesizeMouseAtCenter(tab1, {});

  // Test adding split view with one tab and new tab

  let tabToClick = tab1;
  EventUtils.synthesizeMouseAtCenter(tab1, {});
  let openTabsPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "about:opentabs"
  );
  let tabContainer = document.getElementById("tabbrowser-tabs");
  let splitViewCreated = BrowserTestUtils.waitForEvent(
    tabContainer,
    "SplitViewCreated"
  );
  await withTabMenu(tabToClick, async moveTabToNewSplitViewItem => {
    await BrowserTestUtils.waitForMutationCondition(
      moveTabToNewSplitViewItem,
      { attributes: true },
      () =>
        !moveTabToNewSplitViewItem.hidden &&
        !moveTabToNewSplitViewItem.disabled,
      "moveTabToNewSplitViewItem is visible and not disabled"
    );
    Assert.ok(
      !moveTabToNewSplitViewItem.hidden && !moveTabToNewSplitViewItem.disabled,
      "moveTabToNewSplitViewItem is visible and not disabled"
    );

    info("Click menu option to add new split view");
    moveTabToNewSplitViewItem.click();
    await splitViewCreated;
    await openTabsPromise;
    info("about:opentabs has been opened");
    Assert.equal(
      gBrowser.selectedTab.linkedBrowser.currentURI.spec,
      "about:opentabs",
      "about:opentabs is active in split view"
    );
  });

  let splitview = tab1.splitview;

  Assert.equal(tab1.splitview, splitview, `tab1 is in split view`);
  let aboutOpenTabsDocument =
    gBrowser.selectedTab.linkedBrowser.contentDocument;
  let openTabsComponent = await TestUtils.waitForCondition(
    () => aboutOpenTabsDocument.querySelector("splitview-opentabs"),
    "Open tabs component rendered"
  );
  await TestUtils.waitForCondition(
    () => openTabsComponent.nonSplitViewUnpinnedTabs?.length,
    "Open tabs component has rendered items"
  );

  Assert.equal(
    openTabsComponent.nonSplitViewUnpinnedTabs.length,
    3,
    "3 tabs are shown in the open tabs list"
  );

  await TestUtils.waitForCondition(
    () => openTabsComponent.sidebarTabList.shadowRoot,
    "Open tabs component has shadowRoot"
  );
  await openTabsComponent.sidebarTabList.updateComplete;
  await BrowserTestUtils.waitForMutationCondition(
    openTabsComponent.sidebarTabList.shadowRoot,
    { childList: true, subtree: true },
    () => openTabsComponent.sidebarTabList.rowEls.length === 3,
    "Tabs are shown in the open tabs list"
  );

  Assert.ok(
    openTabsComponent.sidebarTabList.rowEls[1].__url ===
      tab2.linkedBrowser.currentURI.spec &&
      openTabsComponent.sidebarTabList.rowEls[2].__url ===
        tab3.linkedBrowser.currentURI.spec,
    "tab2 and tab3 are listed on the about:opentabs page"
  );

  let aboutOpenTabsWindow = document.querySelector(
    "hbox.deck-selected.split-view-panel browser"
  ).contentWindow;
  openTabsComponent.sidebarTabList.rowEls[0].focus();

  info("Focus the next row.");
  await focusWithKeyboard(
    openTabsComponent.sidebarTabList.rowEls[1],
    "KEY_ArrowDown",
    aboutOpenTabsWindow
  );

  info("Focus the previous row.");
  await focusWithKeyboard(
    openTabsComponent.sidebarTabList.rowEls[0],
    "KEY_ArrowUp",
    aboutOpenTabsWindow
  );

  info("Focus the next row.");
  await focusWithKeyboard(
    openTabsComponent.sidebarTabList.rowEls[1],
    "KEY_ArrowDown",
    aboutOpenTabsWindow
  );

  info("Focus the next row.");
  await focusWithKeyboard(
    openTabsComponent.sidebarTabList.rowEls[2],
    "KEY_ArrowDown",
    aboutOpenTabsWindow
  );

  info("Focus the previous row.");
  await focusWithKeyboard(
    openTabsComponent.sidebarTabList.rowEls[1],
    "KEY_ArrowUp",
    aboutOpenTabsWindow
  );

  info("Open the focused link.");
  EventUtils.synthesizeKey("KEY_Enter", {}, aboutOpenTabsWindow);
  await TestUtils.waitForCondition(
    () => splitview.tabs.includes(tab2),
    "We've opened tab2 in the split view"
  );

  splitview.unsplitTabs();

  // Ensure about:opentabs tab is closed when separating the split view via the context menu

  tabToClick = tab1;
  EventUtils.synthesizeMouseAtCenter(tab1, {});
  openTabsPromise = BrowserTestUtils.waitForNewTab(gBrowser, "about:opentabs");
  tabContainer = document.getElementById("tabbrowser-tabs");
  splitViewCreated = BrowserTestUtils.waitForEvent(
    tabContainer,
    "SplitViewCreated"
  );
  await withTabMenu(tabToClick, async moveTabToNewSplitViewItem => {
    await BrowserTestUtils.waitForMutationCondition(
      moveTabToNewSplitViewItem,
      { attributes: true },
      () =>
        !moveTabToNewSplitViewItem.hidden &&
        !moveTabToNewSplitViewItem.disabled,
      "moveTabToNewSplitViewItem is visible and not disabled"
    );
    Assert.ok(
      !moveTabToNewSplitViewItem.hidden && !moveTabToNewSplitViewItem.disabled,
      "moveTabToNewSplitViewItem is visible and not disabled"
    );

    info("Click menu option to add new split view");
    moveTabToNewSplitViewItem.click();
    await splitViewCreated;
    await openTabsPromise;
    info("about:opentabs has been opened");
    Assert.equal(
      gBrowser.selectedTab.linkedBrowser.currentURI.spec,
      "about:opentabs",
      "about:opentabs is active in split view"
    );
  });

  await withTabMenu(
    tabToClick,
    async (moveTabToNewSplitViewItem, unloadTabItem, unsplitTabItem) => {
      await BrowserTestUtils.waitForMutationCondition(
        unsplitTabItem,
        { attributes: true },
        () => !unsplitTabItem.hidden,
        "unsplitTabItem is visible"
      );
      Assert.ok(!unsplitTabItem.hidden, "unsplitTabItem is visible");

      info("Unsplit split view using menu option");
      unsplitTabItem.click();
    }
  );

  Assert.ok(
    !gBrowser.tabs.some(
      openTab => openTab.linkedBrowser.currentURI.spec === "about:opentabs"
    ),
    "The about:opentabs page has been closed upon separation of the split view"
  );

  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
  }
});

add_task(async function test_containerIndicators() {
  const tab1 = await addTab();
  const tab2 = await addTab();

  // Load a page in a container tab
  let userContextId = 1;
  let containerTab = BrowserTestUtils.addTab(
    gBrowser,
    "http://mochi.test:8888/",
    {
      userContextId,
    }
  );

  await BrowserTestUtils.browserLoaded(
    containerTab.linkedBrowser,
    false,
    "http://mochi.test:8888/"
  );

  // Click the first tab in our test split view to make sure the default tab at the
  // start of the tab strip is deselected
  EventUtils.synthesizeMouseAtCenter(tab1, {});

  // Test adding split view with one tab and new tab

  let tabToClick = tab1;
  EventUtils.synthesizeMouseAtCenter(tab1, {});
  let openTabsPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "about:opentabs"
  );
  let tabContainer = document.getElementById("tabbrowser-tabs");
  let splitViewCreated = BrowserTestUtils.waitForEvent(
    tabContainer,
    "SplitViewCreated"
  );
  await withTabMenu(tabToClick, async moveTabToNewSplitViewItem => {
    await BrowserTestUtils.waitForMutationCondition(
      moveTabToNewSplitViewItem,
      { attributes: true },
      () =>
        !moveTabToNewSplitViewItem.hidden &&
        !moveTabToNewSplitViewItem.disabled,
      "moveTabToNewSplitViewItem is visible and not disabled"
    );
    Assert.ok(
      !moveTabToNewSplitViewItem.hidden && !moveTabToNewSplitViewItem.disabled,
      "moveTabToNewSplitViewItem is visible and not disabled"
    );

    info("Click menu option to add new split view");
    moveTabToNewSplitViewItem.click();
    await splitViewCreated;
    await openTabsPromise;
    info("about:opentabs has been opened");
    Assert.equal(
      gBrowser.selectedTab.linkedBrowser.currentURI.spec,
      "about:opentabs",
      "about:opentabs is active in split view"
    );
  });

  let splitview = tab1.splitview;

  Assert.equal(tab1.splitview, splitview, `tab1 is in split view`);
  let aboutOpenTabsDocument =
    gBrowser.selectedTab.linkedBrowser.contentDocument;
  let openTabsComponent = await TestUtils.waitForCondition(
    () => aboutOpenTabsDocument.querySelector("splitview-opentabs"),
    "Open tabs component rendered"
  );
  await TestUtils.waitForCondition(
    () => openTabsComponent.nonSplitViewUnpinnedTabs?.length,
    "Open tabs component has rendered items"
  );

  Assert.equal(
    openTabsComponent.nonSplitViewUnpinnedTabs.length,
    3,
    "3 tabs are shown in the open tabs list"
  );

  await TestUtils.waitForCondition(
    () => openTabsComponent.sidebarTabList.shadowRoot,
    "Open tabs component has shadowRoot"
  );
  await openTabsComponent.sidebarTabList.updateComplete;
  await BrowserTestUtils.waitForMutationCondition(
    openTabsComponent.sidebarTabList.shadowRoot,
    { childList: true, subtree: true },
    () => openTabsComponent.sidebarTabList.rowEls.length === 3,
    "Tabs are shown in the open tabs list"
  );

  Assert.ok(
    openTabsComponent.sidebarTabList.rowEls[1].__url ===
      tab2.linkedBrowser.currentURI.spec &&
      openTabsComponent.sidebarTabList.rowEls[2].__url ===
        containerTab.linkedBrowser.currentURI.spec,
    "tab2 and tab3 are listed on the about:opentabs page"
  );

  await TestUtils.waitForCondition(
    () =>
      containerTab.getAttribute("usercontextid") === userContextId.toString(),
    "The container tab doesn't have the usercontextid attribute."
  );

  let containerTabElem;

  await TestUtils.waitForCondition(
    () =>
      Array.from(openTabsComponent.sidebarTabList.rowEls).some(rowEl => {
        let hasContainerObj;
        if (rowEl.containerObj?.icon) {
          containerTabElem = rowEl;
          hasContainerObj = rowEl.containerObj;
        }

        return hasContainerObj;
      }),
    "The container tab element isn't marked in about:opentabs."
  );

  Assert.ok(
    containerTabElem.shadowRoot
      .querySelector(".fxview-tab-row-container-indicator")
      .classList.contains("identity-color-blue"),
    "The container color is blue."
  );

  info("The open tab is marked as a container tab.");

  splitview.unsplitTabs();
  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
  }
});

add_task(async function test_aboutOpenTabsReplacedWhenLeftViewActive() {
  const tab1 = await addTab();
  const tab2 = await addTab();
  await addTab();

  EventUtils.synthesizeMouseAtCenter(tab1, {});

  let openTabsPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "about:opentabs"
  );
  let tabContainer = document.getElementById("tabbrowser-tabs");
  let splitViewCreated = BrowserTestUtils.waitForEvent(
    tabContainer,
    "SplitViewCreated"
  );
  await withTabMenu(tab1, async moveTabToNewSplitViewItem => {
    await BrowserTestUtils.waitForMutationCondition(
      moveTabToNewSplitViewItem,
      { attributes: true },
      () =>
        !moveTabToNewSplitViewItem.hidden &&
        !moveTabToNewSplitViewItem.disabled,
      "moveTabToNewSplitViewItem is visible and not disabled"
    );

    info("Click menu option to add new split view");
    moveTabToNewSplitViewItem.click();
    await splitViewCreated;
    await openTabsPromise;
    info("about:opentabs has been opened");
  });

  let splitview = tab1.splitview;
  let aboutOpenTabsTab = gBrowser.selectedTab;
  Assert.equal(
    aboutOpenTabsTab.linkedBrowser.currentURI.spec,
    "about:opentabs",
    "about:opentabs is the selected tab"
  );

  let aboutOpenTabsTabIndex = gBrowser.tabs.indexOf(aboutOpenTabsTab);
  info(`about:opentabs is at index ${aboutOpenTabsTabIndex}`);

  EventUtils.synthesizeMouseAtCenter(tab1, {});
  await TestUtils.waitForCondition(
    () => gBrowser.selectedTab === tab1,
    "tab1 is now selected (left view is active)"
  );

  let aboutOpenTabsDocument = aboutOpenTabsTab.linkedBrowser.contentDocument;
  let openTabsComponent = await TestUtils.waitForCondition(
    () => aboutOpenTabsDocument.querySelector("splitview-opentabs"),
    "Open tabs component rendered"
  );
  await TestUtils.waitForCondition(
    () => openTabsComponent.nonSplitViewUnpinnedTabs?.length,
    "Open tabs component has rendered items"
  );

  await TestUtils.waitForCondition(
    () => openTabsComponent.sidebarTabList.shadowRoot,
    "Open tabs component has shadowRoot"
  );
  await openTabsComponent.sidebarTabList.updateComplete;
  await BrowserTestUtils.waitForMutationCondition(
    openTabsComponent.sidebarTabList.shadowRoot,
    { childList: true, subtree: true },
    () => openTabsComponent.sidebarTabList.rowEls.length,
    "Tabs are shown in the open tabs list"
  );

  let tabRowForTab2 = Array.from(openTabsComponent.sidebarTabList.rowEls).find(
    rowEl => rowEl.__url === tab2.linkedBrowser.currentURI.spec
  );
  Assert.ok(tabRowForTab2, "Found the row element for tab2");

  info(
    "Click tab2 in the about:opentabs list while tab1 (left view) is active"
  );
  EventUtils.synthesizeMouseAtCenter(
    tabRowForTab2,
    {},
    tabRowForTab2.documentGlobal
  );

  await TestUtils.waitForCondition(
    () => splitview.tabs.includes(tab2),
    "tab2 is now in the split view"
  );

  Assert.ok(
    !gBrowser.tabs.some(
      openTab => openTab.linkedBrowser.currentURI.spec === "about:opentabs"
    ),
    "about:opentabs tab has been replaced and closed"
  );

  let tab2Index = gBrowser.tabs.indexOf(tab2);
  Assert.equal(
    tab2Index,
    aboutOpenTabsTabIndex,
    "tab2 is at the same position where about:opentabs was (right view)"
  );

  Assert.equal(
    gBrowser.tabs.indexOf(tab1),
    aboutOpenTabsTabIndex - 1,
    "tab1 is still in the left view position"
  );

  splitview.unsplitTabs();
  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
  }
});

add_task(async function test_splitview_with_unloaded_tab() {
  const tab1 = await addTab();
  await addTab();

  // Click the first tab in our test split view to make sure the default tab at the
  // start of the tab strip is deselected
  EventUtils.synthesizeMouseAtCenter(tab1, {});

  // Test adding split view with one tab and new tab

  let tabToClick = tab1;
  let tabContainer = gBrowser.tabContainer;
  let splitViewCreated = BrowserTestUtils.waitForEvent(
    tabContainer,
    "SplitViewCreated"
  );
  await withTabMenu(
    tabToClick,
    async (moveTabToNewSplitViewItem, unloadTabItem) => {
      await BrowserTestUtils.waitForMutationCondition(
        unloadTabItem,
        { attributes: true },
        () => !unloadTabItem.hidden && !unloadTabItem.disabled,
        "unloadTabItem is visible and not disabled"
      );
      Assert.ok(
        !unloadTabItem.hidden && !unloadTabItem.disabled,
        "unloadTabItem is visible and not disabled"
      );

      info("Click menu option to unload tab");
      unloadTabItem.click();
      await BrowserTestUtils.waitForMutationCondition(
        tab1,
        { attributes: true },
        () => tab1.hasAttribute("discarded"),
        "tab1 has been unloaded"
      );
      info("Tab has been unloaded");
    }
  );

  await withTabMenu(tabToClick, async moveTabToNewSplitViewItem => {
    await BrowserTestUtils.waitForMutationCondition(
      moveTabToNewSplitViewItem,
      { attributes: true },
      () =>
        !moveTabToNewSplitViewItem.hidden &&
        !moveTabToNewSplitViewItem.disabled,
      "moveTabToNewSplitViewItem is visible and not disabled"
    );
    Assert.ok(
      !moveTabToNewSplitViewItem.hidden && !moveTabToNewSplitViewItem.disabled,
      "moveTabToNewSplitViewItem is visible and not disabled"
    );

    info("Click menu option to add new split view");
    moveTabToNewSplitViewItem.click();
    await splitViewCreated;
  });

  let splitview = tab1.splitview;

  Assert.equal(tab1.splitview, splitview, `tab1 is in split view`);

  Assert.ok(
    !tab1.hasAttribute("discarded"),
    "tab1 is no longer unloaded once added to split view"
  );

  splitview.unsplitTabs();
  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
  }
});

add_task(async function test_about_opentabs_reverts_to_newtab_when_no_tabs() {
  const tab1 = await addTab();
  const tab2 = await addTab();

  EventUtils.synthesizeMouseAtCenter(tab1, {});

  let openTabsPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "about:opentabs"
  );
  let tabContainer = gBrowser.tabContainer;
  let splitViewCreated = BrowserTestUtils.waitForEvent(
    tabContainer,
    "SplitViewCreated"
  );
  await withTabMenu(tab1, async moveTabToNewSplitViewItem => {
    await BrowserTestUtils.waitForMutationCondition(
      moveTabToNewSplitViewItem,
      { attributes: true },
      () =>
        !moveTabToNewSplitViewItem.hidden &&
        !moveTabToNewSplitViewItem.disabled,
      "moveTabToNewSplitViewItem is visible and not disabled"
    );

    info("Click menu option to add new split view");
    moveTabToNewSplitViewItem.click();
    await splitViewCreated;
    await openTabsPromise;
    info("about:opentabs has been opened");
  });

  let splitview = tab1.splitview;
  let aboutOpenTabsTab = gBrowser.selectedTab;

  Assert.equal(
    aboutOpenTabsTab.linkedBrowser.currentURI.spec,
    "about:opentabs",
    "about:opentabs is the selected tab"
  );

  let aboutOpenTabsDocument = aboutOpenTabsTab.linkedBrowser.contentDocument;
  let openTabsComponent = await TestUtils.waitForCondition(
    () => aboutOpenTabsDocument.querySelector("splitview-opentabs"),
    "Open tabs component rendered"
  );
  await TestUtils.waitForCondition(
    () => openTabsComponent.nonSplitViewUnpinnedTabs?.length,
    "Open tabs component has rendered items"
  );

  let availableTabs = openTabsComponent.nonSplitViewUnpinnedTabs;
  Assert.greaterOrEqual(
    availableTabs.length,
    1,
    `At least 1 tab is shown in the open tabs list (found ${availableTabs.length})`
  );
  Assert.ok(
    availableTabs.includes(tab2),
    "tab2 is in the list of available tabs"
  );

  info(
    `Remove all ${availableTabs.length} available tabs to trigger navigation to about:newtab`
  );
  for (let tab of availableTabs) {
    BrowserTestUtils.removeTab(tab);
  }

  await TestUtils.waitForCondition(
    () => aboutOpenTabsTab.linkedBrowser.currentURI.spec === "about:newtab",
    "about:opentabs has been automatically replaced with about:newtab"
  );

  Assert.equal(
    aboutOpenTabsTab.linkedBrowser.currentURI.spec,
    "about:newtab",
    "The tab now shows about:newtab since there are no tabs to display"
  );

  splitview.unsplitTabs();
  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
  }
});

add_task(async function test_opentabs_search() {
  const tab1 = await addTab();
  // eslint-disable-next-line @microsoft/sdl/no-insecure-url
  const tab2 = BrowserTestUtils.addTab(gBrowser, "http://example.com/");
  await BrowserTestUtils.browserLoaded(tab2.linkedBrowser);
  const tab3 = BrowserTestUtils.addTab(gBrowser, "http://mochi.test:8888/");
  await BrowserTestUtils.browserLoaded(tab3.linkedBrowser);

  EventUtils.synthesizeMouseAtCenter(tab1, {});

  let openTabsPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "about:opentabs"
  );
  let tabContainer = gBrowser.tabContainer;
  let splitViewCreated = BrowserTestUtils.waitForEvent(
    tabContainer,
    "SplitViewCreated"
  );
  await withTabMenu(tab1, async moveTabToNewSplitViewItem => {
    info("Click menu option to add new split view");
    moveTabToNewSplitViewItem.click();
    await splitViewCreated;
    await openTabsPromise;
    info("about:opentabs has been opened");
  });

  let splitview = tab1.splitview;
  let aboutOpenTabsTab = gBrowser.selectedTab;
  let aboutOpenTabsDocument = aboutOpenTabsTab.linkedBrowser.contentDocument;
  let openTabsComponent = await TestUtils.waitForCondition(
    () => aboutOpenTabsDocument.querySelector("splitview-opentabs"),
    "Open tabs component rendered"
  );

  await TestUtils.waitForCondition(
    () => openTabsComponent.nonSplitViewUnpinnedTabs?.length >= 2,
    "Open tabs component has rendered items"
  );

  await TestUtils.waitForCondition(
    () => openTabsComponent.sidebarTabList.shadowRoot,
    "Open tabs component has shadowRoot"
  );
  await openTabsComponent.sidebarTabList.updateComplete;

  const searchTextbox = openTabsComponent.searchTextbox;
  Assert.ok(searchTextbox, "Search textbox is present");

  info("Input a search query for 'example'.");
  openTabsComponent.searchQuery = "example";
  await openTabsComponent.updateComplete;

  await TestUtils.waitForCondition(
    () =>
      openTabsComponent.shadowRoot?.querySelector(
        "moz-card[data-l10n-id=opentabs-search-results-header]"
      ),
    "Search results header appears"
  );

  await TestUtils.waitForCondition(() => {
    const tabList = openTabsComponent.sidebarTabList;
    return (
      tabList &&
      tabList.rowEls.length === 1 &&
      tabList.rowEls[0].__url.includes("example.com")
    );
  }, "Only the tab with 'example' in the URL is shown in search results");

  info("Input a bogus search query.");
  openTabsComponent.searchQuery = "ThisWillNeverMatchAnyTab123";
  await openTabsComponent.updateComplete;

  await TestUtils.waitForCondition(() => {
    const emptyMessage = openTabsComponent.shadowRoot?.querySelector(
      ".empty-search-message"
    );
    return emptyMessage;
  }, "Empty search message is displayed when no results found");

  info("Clear the search query.");
  openTabsComponent.searchQuery = "";
  await openTabsComponent.updateComplete;

  await TestUtils.waitForCondition(() => {
    const searchResultsHeader = openTabsComponent.shadowRoot?.querySelector(
      "moz-card[data-l10n-id=opentabs-search-results-header]"
    );
    return !searchResultsHeader;
  }, "Search results header is removed after clearing search");

  await TestUtils.waitForCondition(() => {
    const tabList = openTabsComponent.sidebarTabList;
    return tabList && tabList.rowEls.length >= 2;
  }, "All tabs are shown again after clearing search");

  splitview.unsplitTabs();
  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
  }
});

add_task(async function test_open_link_in_split_view_from_container() {
  const USER_CONTEXT_ID = 2;
  const testPageUrl = httpURL("file_anchor_elements.html");

  let containerTab = BrowserTestUtils.addTab(gBrowser, testPageUrl, {
    userContextId: USER_CONTEXT_ID,
    skipAnimation: true,
  });
  gBrowser.selectedTab = containerTab;
  await BrowserTestUtils.browserLoaded(
    containerTab.linkedBrowser,
    false,
    testPageUrl
  );
  let containerIndex = containerTab.elementIndex;
  let splitViewCreated = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "SplitViewCreated"
  );

  const contextMenu = document.getElementById("contentAreaContextMenu");
  let popupShown = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");

  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#a_with_href",
    { type: "contextmenu", button: 2 },
    containerTab.linkedBrowser
  );
  await popupShown;

  let openLinkInSplitViewItem = contextMenu.querySelector(
    "#context-openlinkinsplitview"
  );
  ok(openLinkInSplitViewItem, "Open Link in Split View menu item exists");
  ok(
    BrowserTestUtils.isVisible(openLinkInSplitViewItem),
    "Open Link in Split View menu item is visible"
  );

  let popupHidden = BrowserTestUtils.waitForPopupEvent(contextMenu, "hidden");
  contextMenu.activateItem(openLinkInSplitViewItem);
  await popupHidden;

  await splitViewCreated;
  info("Split view created");

  let linkTab = containerTab.splitview?.tabs.find(tab => tab !== containerTab);
  ok(linkTab, "Link tab should be in the split view");

  is(
    linkTab.userContextId,
    USER_CONTEXT_ID,
    "Link tab should be in the same container as the original tab"
  );

  ok(linkTab.splitview, "Link tab should be in a split view");
  ok(containerTab.splitview, "Container tab should be in a split view");
  is(
    linkTab.splitview,
    containerTab.splitview,
    "Both tabs should be in the same split view"
  );
  is(
    containerIndex,
    linkTab.splitview.elementIndex,
    "Splitview is created in place"
  );

  is(gBrowser.selectedTab, linkTab, "Link tab should be selected");

  linkTab.splitview.close();
  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
  }
});

add_task(async function test_open_link_in_split_view_hidden_on_hidden_tab() {
  FirefoxViewHandler.openTab();
  let fxviewTab = FirefoxViewHandler.tab;
  await BrowserTestUtils.browserLoaded(
    fxviewTab.linkedBrowser,
    false,
    "about:firefoxview"
  );

  ok(fxviewTab.hidden, "Firefox View tab is a hidden tab");

  let browser = fxviewTab.linkedBrowser;
  let doc = browser.contentWindow.document;

  let openTabs = doc.querySelector("view-opentabs[slot='opentabs']");
  await TestUtils.waitForCondition(
    () => openTabs.viewCards?.[0]?.tabList?.rowEls?.length,
    "Open tab rows rendered"
  );

  let firstTabLink = openTabs.viewCards[0].tabList.rowEls[0].mainEl;

  const contextMenu = document.getElementById("contentAreaContextMenu");
  let popupShown = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
  EventUtils.synthesizeMouseAtCenter(
    firstTabLink,
    { type: "contextmenu", button: 2 },
    browser.contentWindow
  );
  await popupShown;

  let openLinkInSplitViewItem = contextMenu.querySelector(
    "#context-openlinkinsplitview"
  );
  ok(openLinkInSplitViewItem, "Open Link in Split View menu item exists");
  ok(
    !BrowserTestUtils.isVisible(openLinkInSplitViewItem),
    "Open Link in Split View menu item is hidden on a hidden tab"
  );

  let popupHidden = BrowserTestUtils.waitForPopupEvent(contextMenu, "hidden");
  contextMenu.hidePopup();
  await popupHidden;

  BrowserTestUtils.removeTab(fxviewTab);
  FirefoxViewHandler.tab = null;
});

add_task(
  async function test_open_link_in_split_view_hidden_when_in_split_view() {
    const testPageUrl = httpURL("file_anchor_elements.html");

    let tab1 = BrowserTestUtils.addTab(gBrowser, testPageUrl, {
      skipAnimation: true,
    });
    await BrowserTestUtils.browserLoaded(
      tab1.linkedBrowser,
      false,
      testPageUrl
    );

    let tab2 = BrowserTestUtils.addTab(gBrowser, testPageUrl, {
      skipAnimation: true,
    });
    await BrowserTestUtils.browserLoaded(
      tab2.linkedBrowser,
      false,
      testPageUrl
    );

    gBrowser.addTabSplitView([tab1, tab2]);
    gBrowser.selectedTab = tab1;

    const contextMenu = document.getElementById("contentAreaContextMenu");
    let popupShown = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");

    await BrowserTestUtils.synthesizeMouseAtCenter(
      "#a_with_href",
      { type: "contextmenu", button: 2 },
      tab1.linkedBrowser
    );
    await popupShown;

    let openLinkInSplitViewItem = contextMenu.querySelector(
      "#context-openlinkinsplitview"
    );
    ok(openLinkInSplitViewItem, "Open Link in Split View menu item exists");
    ok(
      !BrowserTestUtils.isVisible(openLinkInSplitViewItem),
      "Open Link in Split View menu item is hidden when current tab is in a split view"
    );

    let popupHidden = BrowserTestUtils.waitForPopupEvent(contextMenu, "hidden");
    contextMenu.hidePopup();
    await popupHidden;

    tab1.splitview.close();
    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
    }
  }
);

add_task(async function test_about_opentabs_cannot_be_pinned() {
  const tab1 = await addTab();
  const tab2 = await addTab();

  EventUtils.synthesizeMouseAtCenter(tab1, {});

  let openTabsPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "about:opentabs"
  );
  let splitViewCreated = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "SplitViewCreated"
  );
  await withTabMenu(tab1, async moveTabToNewSplitViewItem => {
    await BrowserTestUtils.waitForMutationCondition(
      moveTabToNewSplitViewItem,
      { attributes: true },
      () =>
        !moveTabToNewSplitViewItem.hidden &&
        !moveTabToNewSplitViewItem.disabled,
      "moveTabToNewSplitViewItem is visible and not disabled"
    );

    info("Click menu option to add new split view");
    moveTabToNewSplitViewItem.click();
    await splitViewCreated;
    await openTabsPromise;
  });

  let aboutOpenTabsTab = gBrowser.selectedTab;
  Assert.equal(
    aboutOpenTabsTab.linkedBrowser.currentURI.spec,
    "about:opentabs",
    "about:opentabs is the selected tab"
  );

  let menuItemPinTab = document.getElementById("context_pinTab");
  let menuItemPinSelectedTabs = document.getElementById(
    "context_pinSelectedTabs"
  );

  // Right-click the about:opentabs tab on its own.
  updateTabContextMenu(aboutOpenTabsTab);
  Assert.ok(
    menuItemPinTab.hidden,
    "Pin Tab is hidden when right-clicking the about:opentabs tab"
  );

  // Multi-select the about:opentabs tab alongside another tab and confirm
  // Pin Selected Tabs is hidden.
  await triggerClickOn(tab2, { ctrlKey: true });
  Assert.ok(aboutOpenTabsTab.multiselected, "about:opentabs is multiselected");
  Assert.ok(tab2.multiselected, "tab2 is multiselected");

  updateTabContextMenu(aboutOpenTabsTab);
  Assert.ok(
    menuItemPinSelectedTabs.hidden,
    "Pin Selected Tabs is hidden when about:opentabs is in the selection"
  );

  // Right-clicking a different multiselected tab should still hide the item
  // because the selection contains about:opentabs.
  updateTabContextMenu(tab2);
  Assert.ok(
    menuItemPinSelectedTabs.hidden,
    "Pin Selected Tabs is hidden when right-clicking a sibling of about:opentabs"
  );

  tab1.splitview.unsplitTabs();
  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
  }
});
