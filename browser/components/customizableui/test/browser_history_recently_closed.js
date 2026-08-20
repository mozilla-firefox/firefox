/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { SessionStoreTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SessionStoreTestUtils.sys.mjs"
);
const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);
const triggeringPrincipal_base64 = E10SUtils.SERIALIZED_SYSTEMPRINCIPAL;

SessionStoreTestUtils.init(this, window);

const TEST_PATH = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "https://example.com"
);

let panelMenuWidgetAdded = false;
function prepareHistoryPanel() {
  if (panelMenuWidgetAdded) {
    return;
  }
  CustomizableUI.addWidgetToArea(
    "history-panelmenu",
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL
  );
  registerCleanupFunction(() => CustomizableUI.reset());
}

async function openRecentlyClosedTabsMenu(doc = window.document) {
  prepareHistoryPanel();
  await openHistoryPanel(doc);

  let recentlyClosedTabs = doc.getElementById("appMenuRecentlyClosedTabs");
  Assert.ok(
    !recentlyClosedTabs.getAttribute("disabled"),
    "Recently closed tabs button enabled"
  );
  let closeTabsPanel = doc.getElementById("appMenu-library-recentlyClosedTabs");
  let panelView = closeTabsPanel && PanelView.forNode(closeTabsPanel);
  if (!panelView?.active) {
    recentlyClosedTabs.click();
    closeTabsPanel = doc.getElementById("appMenu-library-recentlyClosedTabs");
    await BrowserTestUtils.waitForEvent(closeTabsPanel, "ViewShown");
    ok(
      PanelView.forNode(closeTabsPanel)?.active,
      "Opened 'Recently closed tabs' panel"
    );
  }

  return closeTabsPanel;
}

function makeTabState(url) {
  return {
    entries: [{ url, triggeringPrincipal_base64 }],
  };
}

function makeClosedTabState(url, { groupId, closedInTabGroup = false } = {}) {
  return {
    title: url,
    closedInTabGroupId: closedInTabGroup ? groupId : null,
    state: {
      entries: [
        {
          url,
          triggeringPrincipal_base64,
        },
      ],
      groupId,
    },
  };
}

function resetClosedTabsAndWindows() {
  // Clear the lists of closed windows and tabs.
  Services.obs.notifyObservers(null, "browser:purge-session-history");
  is(SessionStore.getClosedWindowCount(), 0, "Expect 0 closed windows");
  for (const win of BrowserWindowTracker.orderedWindows) {
    is(
      SessionStore.getClosedTabCountForWindow(win),
      0,
      "Expect 0 closed tabs for this window"
    );
  }
}

registerCleanupFunction(async () => {
  await resetClosedTabsAndWindows();
});

add_task(async function testRecentlyClosedDisabled() {
  info("Check history recently closed tabs/windows section");

  prepareHistoryPanel();
  // We need to make sure the history is cleared before starting the test
  await Sanitizer.sanitize(["history"]);

  await openHistoryPanel();

  let recentlyClosedTabs = document.getElementById("appMenuRecentlyClosedTabs");
  let recentlyClosedWindows = document.getElementById(
    "appMenuRecentlyClosedWindows"
  );

  // Wait for the disabled attribute to change, as we receive
  // the "viewshown" event before this changes
  await TestUtils.waitForCondition(
    () => recentlyClosedTabs.hasAttribute("disabled"),
    "Waiting for button to become disabled"
  );
  Assert.ok(
    recentlyClosedTabs.hasAttribute("disabled"),
    "Recently closed tabs button disabled"
  );
  Assert.ok(
    recentlyClosedWindows.hasAttribute("disabled"),
    "Recently closed windows button disabled"
  );

  await hideHistoryPanel();

  gBrowser.selectedTab.focus();
  await SessionStoreTestUtils.openAndCloseTab(
    window,
    TEST_PATH + "dummy_history_item.html"
  );

  await openHistoryPanel();

  await TestUtils.waitForCondition(
    () => !recentlyClosedTabs.hasAttribute("disabled"),
    "Waiting for button to be enabled"
  );
  Assert.ok(
    !recentlyClosedTabs.hasAttribute("disabled"),
    "Recently closed tabs is available"
  );
  Assert.ok(
    recentlyClosedWindows.hasAttribute("disabled"),
    "Recently closed windows button disabled"
  );

  await hideHistoryPanel();

  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  let loadedPromise = BrowserTestUtils.browserLoaded(
    newWin.gBrowser.selectedBrowser
  );
  BrowserTestUtils.startLoadingURIString(
    newWin.gBrowser.selectedBrowser,
    "about:mozilla"
  );
  await loadedPromise;
  await BrowserTestUtils.closeWindow(newWin);

  await openHistoryPanel();

  await TestUtils.waitForCondition(
    () => !recentlyClosedWindows.hasAttribute("disabled"),
    "Waiting for button to be enabled"
  );
  Assert.ok(
    !recentlyClosedTabs.hasAttribute("disabled"),
    "Recently closed tabs is available"
  );
  Assert.ok(
    !recentlyClosedWindows.hasAttribute("disabled"),
    "Recently closed windows is available"
  );

  await hideHistoryPanel();
});

add_task(async function testRecentlyClosedTabsDisabledPersists() {
  info("Check history recently closed tabs/windows section");

  prepareHistoryPanel();

  // We need to make sure the history is cleared before starting the test
  await Sanitizer.sanitize(["history"]);

  await openHistoryPanel();

  let recentlyClosedTabs = document.getElementById("appMenuRecentlyClosedTabs");
  Assert.ok(
    recentlyClosedTabs.hasAttribute("disabled"),
    "Recently closed tabs button disabled"
  );

  await hideHistoryPanel();

  let newWin = await BrowserTestUtils.openNewBrowserWindow();

  await openHistoryPanel(newWin.document);
  recentlyClosedTabs = newWin.document.getElementById(
    "appMenuRecentlyClosedTabs"
  );
  Assert.ok(
    recentlyClosedTabs.hasAttribute("disabled"),
    "Recently closed tabs is disabled"
  );

  // We close the window without hiding the panel first, which used to interfere
  // with populating the view subsequently.
  await BrowserTestUtils.closeWindow(newWin);

  newWin = await BrowserTestUtils.openNewBrowserWindow();
  await openHistoryPanel(newWin.document);
  recentlyClosedTabs = newWin.document.getElementById(
    "appMenuRecentlyClosedTabs"
  );
  Assert.ok(
    recentlyClosedTabs.hasAttribute("disabled"),
    "Recently closed tabs is disabled"
  );
  await hideHistoryPanel(newWin.document);
  await BrowserTestUtils.closeWindow(newWin);
});

add_task(async function testRecentlyClosedRestoreAllTabs() {
  // We need to make sure the history is cleared before starting the test
  await Sanitizer.sanitize(["history"]);
  await resetClosedTabsAndWindows();
  const initialTabCount = gBrowser.visibleTabs.length;

  const closedTabUrls = [
    "about:robots",
    "https://example.com/",
    "https://example.org/",
  ];

  const closedTabGroupInOpenWindowUrls = ["about:mozilla", "about:mozilla"];
  const closedTabGroupInOpenWindowId = "1234567890-1";

  const closedTabGroupInClosedWindowUrls = ["about:robots", "about:robots"];
  const closedTabGroupInClosedWindowId = "1234567890-2";

  await SessionStoreTestUtils.promiseBrowserState({
    windows: [
      {
        tabs: [makeTabState("about:mozilla")],
        _closedTabs: closedTabUrls.map(makeClosedTabState),
        closedGroups: [
          {
            collapsed: false,
            color: "blue",
            id: closedTabGroupInOpenWindowId,
            name: "closed-in-open-window",
            tabs: closedTabGroupInOpenWindowUrls.map(url =>
              makeClosedTabState(url, { groupId: closedTabGroupInOpenWindowId })
            ),
          },
        ],
      },
    ],
    _closedWindows: [
      {
        tabs: [makeTabState("about:mozilla")],
        _closedTabs: [],
        closedGroups: [
          {
            collapsed: false,
            color: "red",
            id: closedTabGroupInClosedWindowId,
            name: "closed-in-closed-window",
            tabs: closedTabGroupInClosedWindowUrls.map(url =>
              makeClosedTabState(url, {
                groupId: closedTabGroupInClosedWindowId,
              })
            ),
          },
        ],
      },
    ],
  });

  is(gBrowser.visibleTabs.length, 1, "We start with one tab open");
  // Open the "Recently closed tabs" panel.
  let closeTabsPanel = await openRecentlyClosedTabsMenu();

  // Click the first toolbar button in the panel.
  let toolbarButton = closeTabsPanel.querySelector(
    ".panel-subview-body toolbarbutton"
  );
  let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, null, true);
  EventUtils.sendMouseEvent({ type: "click" }, toolbarButton, window);

  info(
    "We should reopen the first of closedTabUrls: " +
      JSON.stringify(closedTabUrls)
  );
  let reopenedTab = await newTabPromise;
  is(
    reopenedTab.linkedBrowser.currentURI.spec,
    closedTabUrls[0],
    "Opened the first URL"
  );
  info(`restored tab, total open tabs: ${gBrowser.tabs.length}`);

  info("waiting for closeTab");
  await SessionStoreTestUtils.closeTab(reopenedTab);

  await openRecentlyClosedTabsMenu();
  let restoreAllItem = closeTabsPanel.querySelector(".restoreallitem");
  ok(
    restoreAllItem && !restoreAllItem.hidden,
    "Restore all menu item is not hidden"
  );

  // Click the restore-all toolbar button in the panel.
  EventUtils.sendMouseEvent({ type: "click" }, restoreAllItem, window);

  info("waiting for restored tabs");
  await TestUtils.waitForCondition(
    () => SessionStore.getClosedTabCount() === 0,
    "Waiting for all the closed tabs to be opened"
  );

  is(
    gBrowser.tabs.length,
    initialTabCount +
      closedTabUrls.length +
      closedTabGroupInOpenWindowUrls.length +
      closedTabGroupInClosedWindowUrls.length,
    "The expected number of closed tabs were restored"
  );
  is(
    gBrowser.tabGroups[0].id,
    closedTabGroupInOpenWindowId,
    "Closed tab group in open window was restored"
  );
  closedTabGroupInOpenWindowUrls.forEach((expectedUrl, index) => {
    is(
      gBrowser.tabGroups[0].tabs[index].linkedBrowser.currentURI.spec,
      expectedUrl,
      `Closed tab group in open window tab #${index} has correct URL`
    );
  });
  is(
    gBrowser.tabGroups[1].id,
    closedTabGroupInClosedWindowId,
    "Closed tab group in closed window was restored"
  );
  closedTabGroupInClosedWindowUrls.forEach((expectedUrl, index) => {
    is(
      gBrowser.tabGroups[1].tabs[index].linkedBrowser.currentURI.spec,
      expectedUrl,
      `Closed tab group in closed window tab #${index} has correct URL`
    );
  });

  // clean up extra tabs
  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
  }
});

add_task(async function testRecentlyClosedWindows() {
  // We need to make sure the history is cleared before starting the test
  await Sanitizer.sanitize(["history"]);
  await resetClosedTabsAndWindows();

  // Open and close a new window.
  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  let loadedPromise = BrowserTestUtils.browserLoaded(
    newWin.gBrowser.selectedBrowser
  );
  BrowserTestUtils.startLoadingURIString(
    newWin.gBrowser.selectedBrowser,
    "https://example.com"
  );
  await loadedPromise;
  let closedObjectsChangePromise = TestUtils.topicObserved(
    "sessionstore-closed-objects-changed"
  );
  await BrowserTestUtils.closeWindow(newWin);
  await closedObjectsChangePromise;

  prepareHistoryPanel();
  await openHistoryPanel();

  // Open the "Recently closed windows" panel.
  document.getElementById("appMenuRecentlyClosedWindows").click();

  let winPanel = document.getElementById(
    "appMenu-library-recentlyClosedWindows"
  );
  await BrowserTestUtils.waitForEvent(winPanel, "ViewShown");
  ok(true, "Opened 'Recently closed windows' panel");

  // Click the first toolbar button in the panel.
  let panelBody = winPanel.querySelector(".panel-subview-body");
  let toolbarButton = panelBody.querySelector("toolbarbutton");
  let newWindowPromise = BrowserTestUtils.waitForNewWindow({
    url: "https://example.com/",
  });
  closedObjectsChangePromise = TestUtils.topicObserved(
    "sessionstore-closed-objects-changed"
  );
  EventUtils.sendMouseEvent({ type: "click" }, toolbarButton, window);

  newWin = await newWindowPromise;
  await closedObjectsChangePromise;
  is(gBrowser.tabs.length, 1, "Did not open new tabs");

  await BrowserTestUtils.closeWindow(newWin);
});

add_task(async function testRecentlyClosedTabsFromClosedWindows() {
  await resetClosedTabsAndWindows();
  const closedTabUrls = [
    "about:robots",
    "https://example.com/",
    "https://example.org/",
  ];
  const closedWindowState = {
    tabs: [
      {
        entries: [{ url: "about:mozilla", triggeringPrincipal_base64 }],
      },
    ],
    closedGroups: [],
    _closedTabs: closedTabUrls.map(url => {
      return {
        title: url,
        state: {
          entries: [
            {
              url,
              triggeringPrincipal_base64,
            },
          ],
        },
      };
    }),
  };
  await SessionStoreTestUtils.promiseBrowserState({
    windows: [
      {
        tabs: [
          {
            entries: [{ url: "about:mozilla", triggeringPrincipal_base64 }],
          },
        ],
      },
    ],
    _closedWindows: [closedWindowState],
  });
  Assert.equal(
    SessionStore.getClosedTabCountFromClosedWindows(),
    closedTabUrls.length,
    "Sanity check number of closed tabs from closed windows"
  );

  prepareHistoryPanel();
  let closeTabsPanel = await openRecentlyClosedTabsMenu();
  // make sure we can actually restore one of these closed tabs
  const closedTabItems = closeTabsPanel.querySelectorAll(
    "toolbarbutton[targetURI]"
  );
  Assert.equal(
    closedTabItems.length,
    closedTabUrls.length,
    "We have expected number of closed tab items"
  );

  const newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, null, true);
  const closedObjectsChangePromise = TestUtils.topicObserved(
    "sessionstore-closed-objects-changed"
  );
  EventUtils.sendMouseEvent({ type: "click" }, closedTabItems[0], window);
  await newTabPromise;
  await closedObjectsChangePromise;

  // flip the pref so none of the closed tabs from closed window are included
  await SpecialPowers.pushPrefEnv({
    set: [["browser.sessionstore.closedTabsFromClosedWindows", false]],
  });
  await openHistoryPanel();

  // verify the recently-closed-tabs menu item is disabled
  let recentlyClosedTabsItem = document.getElementById(
    "appMenuRecentlyClosedTabs"
  );
  Assert.ok(
    recentlyClosedTabsItem.hasAttribute("disabled"),
    "Recently closed tabs button is now disabled"
  );
  await hideHistoryPanel();

  SpecialPowers.popPrefEnv();
  while (gBrowser.tabs.length > 1) {
    await SessionStoreTestUtils.closeTab(
      gBrowser.tabs[gBrowser.tabs.length - 1]
    );
  }
});

add_task(async function testRecentlyClosedTabGroupsSingleTab() {
  // We need to make sure the history is cleared before starting the test
  await Sanitizer.sanitize(["history"]);
  await resetClosedTabsAndWindows();
  prepareHistoryPanel();

  is(gBrowser.visibleTabs.length, 1, "We start with one tab already open");

  let aboutMozillaTabA = BrowserTestUtils.addTab(gBrowser, "about:mozilla?foo");
  let aboutMozillaTabB = BrowserTestUtils.addTab(gBrowser, "about:mozilla?bar");
  let mozillaTabGroup = gBrowser.addTabGroup(
    [aboutMozillaTabA, aboutMozillaTabB],
    {
      color: "red",
      label: "mozilla stuff",
    }
  );
  const mozillaTabGroupId = mozillaTabGroup.id;
  const mozillaTabGroupName = mozillaTabGroup.label;
  let aboutRobotsTab = BrowserTestUtils.addTab(gBrowser, "about:robots");

  info("load all of the tabs");
  await Promise.all(
    [aboutMozillaTabA, aboutMozillaTabB, aboutRobotsTab].map(async t => {
      await BrowserTestUtils.browserLoaded(t.linkedBrowser);
      await TabStateFlusher.flush(t.linkedBrowser);
    })
  );

  info("close the tab group and wait for it to be removed");
  let removePromise = BrowserTestUtils.waitForEvent(
    mozillaTabGroup,
    "TabGroupRemoved"
  );
  gBrowser.removeTabGroup(mozillaTabGroup);
  await removePromise;

  is(
    gBrowser.visibleTabs.length,
    2,
    "The tab from before the test plus about:robots should still be open"
  );
  info("Open the 'Recently closed tabs' panel.");
  let closeTabsPanel = await openRecentlyClosedTabsMenu();

  // Click the tab group button in the panel.
  let tabGroupToolbarButton = closeTabsPanel.querySelector(
    `.panel-subview-body toolbarbutton[label="${mozillaTabGroupName}"]`
  );
  ok(tabGroupToolbarButton, "should find the tab group toolbar button");

  let tabGroupPanelview = document.getElementById(
    `closed-tabs-tab-group-${mozillaTabGroupId}`
  );
  ok(tabGroupPanelview, "should find the tab group panelview");

  EventUtils.sendMouseEvent({ type: "click" }, tabGroupToolbarButton, window);
  await BrowserTestUtils.waitForEvent(tabGroupPanelview, "ViewShown");

  info("restore the first tab from the closed tab group");
  let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, null, true);
  let tabToolbarButton = tabGroupPanelview.querySelector(
    ".panel-subview-body toolbarbutton"
  );
  ok(tabToolbarButton, "should find at least one tab to restore");
  let tabTitle = tabToolbarButton.label;
  EventUtils.sendMouseEvent({ type: "click" }, tabToolbarButton, window);

  let reopenedTab = await newTabPromise;
  is(reopenedTab.label, tabTitle, "Opened the first URL");
  info(`restored tab, total open tabs: ${gBrowser.tabs.length}`);

  // clean up extra tabs
  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
  }
});

add_task(async function testRecentlyClosedTabGroupOpensFromAnyWindow() {
  // We need to make sure the history is cleared before starting the test
  await Sanitizer.sanitize(["history"]);
  await resetClosedTabsAndWindows();
  prepareHistoryPanel();

  is(gBrowser.visibleTabs.length, 1, "We start with one tab already open");

  let groupedTab = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  await BrowserTestUtils.browserLoaded(groupedTab.linkedBrowser);
  await TabStateFlusher.flush(groupedTab.linkedBrowser);
  let tabGroup = gBrowser.addTabGroup([groupedTab], { label: "Some group" });
  const tabGroupId = tabGroup.id;
  const tabGroupLabel = tabGroup.label;

  info("close the tab group and wait for it to be removed");
  let removePromise = BrowserTestUtils.waitForEvent(
    tabGroup,
    "TabGroupRemoved"
  );
  gBrowser.removeTabGroup(tabGroup);
  await removePromise;

  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  let closeTabsPanel = await openRecentlyClosedTabsMenu(newWin.document);

  // Click the tab group button in the panel.
  let tabGroupToolbarButton = closeTabsPanel.querySelector(
    `.panel-subview-body toolbarbutton[label="${tabGroupLabel}"]`
  );
  ok(tabGroupToolbarButton, "should find the tab group toolbar button");

  let tabGroupPanelview = newWin.document.getElementById(
    `closed-tabs-tab-group-${tabGroupId}`
  );
  ok(tabGroupPanelview, "should find the tab group panelview");

  EventUtils.sendMouseEvent({ type: "click" }, tabGroupToolbarButton, window);
  await BrowserTestUtils.waitForEvent(tabGroupPanelview, "ViewShown");

  let tabToolbarButton = tabGroupPanelview.querySelector(
    "toolbarbutton.reopentabgroupitem"
  );
  let tabGroupRestored = BrowserTestUtils.waitForEvent(
    newWin.gBrowser.tabContainer,
    "SSTabRestored"
  );
  EventUtils.sendMouseEvent({ type: "click" }, tabToolbarButton, window);
  await tabGroupRestored;

  is(
    newWin.gBrowser.tabGroups.length,
    1,
    "Tab group added to new window tab strip"
  );
  is(
    newWin.gBrowser.tabGroups[0].label,
    tabGroupLabel,
    "Tab group label is the same as it was before restore"
  );

  await BrowserTestUtils.closeWindow(newWin);
});

add_task(async function testRecentlyClosedTabsFromManyWindows() {
  // Ensures that bug1943850 has a proper resolution.
  info(
    "Tabs must be indexed by individual window, even when multiple windows are open"
  );

  await resetClosedTabsAndWindows();
  const ORIG_STATE = SessionStore.getBrowserState();

  await SessionStoreTestUtils.promiseBrowserState({
    windows: [
      {
        tabs: [makeTabState("about:mozilla")],
        _closedTabs: [
          makeClosedTabState("about:mozilla"),
          makeClosedTabState("about:mozilla"),
        ],
      },
      {
        tabs: [makeTabState("about:mozilla")],
        _closedTabs: [makeClosedTabState("about:robots")],
      },
    ],
  });

  Assert.equal(
    SessionStore.getClosedTabCount(),
    3,
    "Sanity check number of closed tabs from closed windows"
  );

  prepareHistoryPanel();
  let closeTabsPanel = await openRecentlyClosedTabsMenu();

  info("make sure we can actually restore one of these closed tabs");
  const closedTabItems = closeTabsPanel.querySelectorAll(
    "toolbarbutton[targetURI]"
  );
  Assert.equal(
    closedTabItems.length,
    3,
    "We have expected number of closed tab items"
  );

  const newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, null, true);
  const closedObjectsChangePromise = TestUtils.topicObserved(
    "sessionstore-closed-objects-changed"
  );
  EventUtils.sendMouseEvent({ type: "click" }, closedTabItems[2], window);
  await newTabPromise;
  await closedObjectsChangePromise;

  Assert.equal(
    gBrowser.tabs[1].linkedBrowser.currentURI.spec,
    "about:robots",
    "Closed tab from the second window is opened"
  );

  await SessionStoreTestUtils.promiseBrowserState(ORIG_STATE);
});

add_task(async function testTabsFromGroupClosedBeforeGroupDeleted() {
  // Ensures that bug1945111 has a proper resolution.
  info(
    "Tabs that were closed from within a tab group should appear in history menus as standalone tabs, even if the tab group is later deleted"
  );

  await resetClosedTabsAndWindows();
  const ORIG_STATE = SessionStore.getBrowserState();
  const GROUP_ID = "1234567890-1";

  await SessionStoreTestUtils.promiseBrowserState({
    windows: [
      {
        tabs: [makeTabState("about:blank")],
        _closedTabs: [
          makeClosedTabState("about:mozilla", { groupId: GROUP_ID }),
        ],
        closedGroups: [
          {
            id: GROUP_ID,
            color: "blue",
            name: "tab-group",
            tabs: [
              makeClosedTabState("about:mozilla", {
                groupId: GROUP_ID,
                closedInTabGroup: true,
              }),
              makeClosedTabState("about:mozilla", {
                groupId: GROUP_ID,
                closedInTabGroup: true,
              }),
              makeClosedTabState("about:mozilla", {
                groupId: GROUP_ID,
                closedInTabGroup: true,
              }),
            ],
          },
        ],
      },
    ],
  });

  Assert.equal(
    SessionStore.getClosedTabCount(),
    4,
    "Sanity check number of closed tabs from closed windows"
  );

  let closeTabsPanel = await openRecentlyClosedTabsMenu();

  const topLevelClosedTabItems = closeTabsPanel
    .querySelector(".panel-subview-body")
    .querySelectorAll(":scope > toolbarbutton[targetURI]");
  Assert.equal(
    topLevelClosedTabItems.length,
    1,
    "We have the expected number of top-level closed tab items"
  );

  const tabGroupClosedTabItems = closeTabsPanel.querySelectorAll(
    `panelview#closed-tabs-tab-group-${GROUP_ID} toolbarbutton[targetURI]`
  );
  Assert.equal(
    tabGroupClosedTabItems.length,
    3,
    "We have the expected number of closed tab items within the tab group"
  );

  await hideHistoryPanel();

  await SessionStoreTestUtils.promiseBrowserState(ORIG_STATE);
});

add_task(async function testOpenTabFromClosedGroupInClosedWindow() {
  // Asserts fix for bug1944416
  info(
    "Open a tab from a closed tab group in a closed window as a standalone tab"
  );
  await Sanitizer.sanitize(["history"]);
  await resetClosedTabsAndWindows();
  const ORIG_STATE = SessionStore.getBrowserState();

  const closedTabUrl = "about:robots";
  const closedTabGroupUrls = ["about:mozilla", "https://example.com"];
  const closedTabGroupId = "1234567890-1";

  await SessionStoreTestUtils.promiseBrowserState({
    windows: [
      {
        tabs: [makeTabState("about:blank")],
        _closedTabs: [],
        closedGroups: [],
      },
    ],
    _closedWindows: [
      {
        tabs: [makeTabState("about:blank")],
        _closedTabs: [makeClosedTabState(closedTabUrl)],
        closedGroups: [
          {
            id: closedTabGroupId,
            color: "red",
            name: "tab-group",
            tabs: closedTabGroupUrls.map(url =>
              makeClosedTabState(url, {
                groupId: closedTabGroupId,
                closedInTabGroup: true,
              })
            ),
          },
        ],
      },
    ],
  });

  Assert.equal(
    SessionStore.getClosedTabCountFromClosedWindows(),
    closedTabGroupUrls.length + 1, // Add the lone ungrouped closed tab
    "Sanity check number of closed tabs from closed windows"
  );

  let closeTabsPanel = await openRecentlyClosedTabsMenu();
  const closedTabItems = closeTabsPanel.querySelectorAll(
    "toolbarbutton[targetURI]"
  );

  let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, null, true);
  let closedObjectsChangePromise = TestUtils.topicObserved(
    "sessionstore-closed-objects-changed"
  );
  EventUtils.sendMouseEvent({ type: "click" }, closedTabItems[0], window);
  await newTabPromise;
  await closedObjectsChangePromise;

  Assert.equal(
    gBrowser.tabs.at(-1).linkedBrowser.currentURI.spec,
    closedTabUrl,
    "Ungrouped closed tab from closed window is opened correctly in the presence of closed tab groups"
  );

  closeTabsPanel = await openRecentlyClosedTabsMenu();
  const tabGroupClosedTabItems = closeTabsPanel.querySelectorAll(
    `panelview#closed-tabs-tab-group-${closedTabGroupId} toolbarbutton[targetURI]`
  );

  newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, null, true);
  closedObjectsChangePromise = TestUtils.topicObserved(
    "sessionstore-closed-objects-changed"
  );
  EventUtils.sendMouseEvent(
    { type: "click" },
    tabGroupClosedTabItems[0],
    window
  );
  await newTabPromise;
  await closedObjectsChangePromise;

  Assert.equal(
    gBrowser.tabs.at(-1).linkedBrowser.currentURI.spec,
    closedTabGroupUrls[0],
    "Grouped closed tab from closed window is opened correctly"
  );

  await SessionStoreTestUtils.promiseBrowserState(ORIG_STATE);
});

add_task(async function testHistoryMenusWorkWithOldPreTabGroupsState() {
  info(
    "Ensure that a session file created before the release of tab groups does not prevent closed tabs in closed windows from opening"
  );
  // Fixes bug1947503

  // We need to make sure the history is cleared before starting the test
  await Sanitizer.sanitize(["history"]);
  await resetClosedTabsAndWindows();
  const ORIG_STATE = SessionStore.getBrowserState();

  const closedTabUrls = [
    "about:robots",
    "https://example.com/",
    "https://example.org/",
  ];

  await SessionStoreTestUtils.promiseBrowserState({
    windows: [
      {
        tabs: [makeTabState("about:mozilla")],
        _closedTabs: [],
        // No closedGroups element
      },
    ],
    _closedWindows: [
      {
        tabs: [makeTabState("about:mozilla")],
        _closedTabs: closedTabUrls.map(makeClosedTabState),
        // No closedGroups element
      },
    ],
  });

  is(gBrowser.visibleTabs.length, 1, "We start with one tab open");
  // Open the "Recently closed tabs" panel.
  let closeTabsPanel = await openRecentlyClosedTabsMenu();

  // Click the first toolbar button in the panel.
  let toolbarButton = closeTabsPanel.querySelector(
    ".panel-subview-body toolbarbutton"
  );
  let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, null, true);
  EventUtils.sendMouseEvent({ type: "click" }, toolbarButton, window);
  let reopenedTab = await newTabPromise;

  is(gBrowser.tabs.length, 2, "Closed tab was restored");

  await SessionStoreTestUtils.closeTab(reopenedTab);
  await SessionStoreTestUtils.promiseBrowserState(ORIG_STATE);
});
