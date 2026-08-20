/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);
const { CustomizableUITestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/CustomizableUITestUtils.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.tabs.groups.enabled", true],
      ["browser.tabs.groups.alternateMenu", true],
    ],
  });
  TabGroupTestUtils.forgetSavedTabGroups();
});

async function createTestGroup(options = {}) {
  let win = options.targetWin ?? window;
  let tab = await addTabTo(win.gBrowser, "data:text/plain,tab", {
    skipAnimation: true,
  });
  await TabStateFlusher.flush(tab.linkedBrowser);
  return win.gBrowser.addTabGroup([tab], options);
}

async function openTabGroupsSubView(win = window) {
  await new CustomizableUITestUtils(win).openMainMenu();
  let subView = win.PanelMultiView.getViewNode(
    win.document,
    "appMenu-tabGroupsListView"
  );
  let viewShown = BrowserTestUtils.waitForEvent(subView, "ViewShown");
  win.PanelMultiView.getViewNode(
    win.document,
    "appMenu-tab-groups-button"
  ).click();
  await viewShown;
  await subView.querySelector("tab-groups-list").updateComplete;
  return subView;
}

async function closeAppMenu(win = window) {
  await new CustomizableUITestUtils(win).hideMainMenu();
}

/**
 * Given a button `buttonToClick` that will create a new tab group containing
 * a new tab, click the button and resolve to the newly opened tab group.
 *
 * @param {Element} buttonToClick
 * @returns {Promise<MozTabbrowserTabGroup>}
 */
async function waitForNewTabGroup(buttonToClick) {
  let initialTabCount = gBrowser.tabs.length;
  let selectedTab = gBrowser.selectedTab;

  let panel = buttonToClick.closest("panel");
  let panelHidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
  let groupCreated = BrowserTestUtils.waitForEvent(
    window,
    "TabGroupCreateByUser"
  );
  // Tab group menu needs to initialize before safely removing the tab group
  let tabGroupMenuShown = BrowserTestUtils.waitForPopupEvent(
    gBrowser.tabGroupMenu.panel,
    "shown"
  );
  buttonToClick.click();
  let [tabGroupCreatedEvent] = await Promise.all([
    groupCreated,
    panelHidden,
    tabGroupMenuShown,
  ]);
  let tabGroup = tabGroupCreatedEvent.target;

  Assert.equal(
    gBrowser.tabs.length,
    initialTabCount + 1,
    "A new tab was opened"
  );
  Assert.equal(tabGroup.tagName, "tab-group", "tab group was created");
  Assert.equal(tabGroup.tabs.length, 1, "tab group has 1 tab");
  Assert.equal(
    tabGroup.tabs[0].linkedBrowser.currentURI.spec,
    window.BROWSER_NEW_TAB_URL,
    "new tab in the group uses the new tab URL"
  );
  Assert.ok(
    !selectedTab.group,
    "The previously selected tab was not added to a group"
  );
  return tabGroup;
}

add_task(async function test_prefChangeControlsVisibility() {
  info("Test that button is visible when pref is true");
  const button = PanelMultiView.getViewNode(
    document,
    "appMenu-tab-groups-button"
  );
  Assert.ok(!button.hidden, "Button is initially visible");

  info("Test that button is hidden when pref is changed to false");
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.groups.alternateMenu", false]],
  });
  Assert.ok(button.hidden, "Button is hidden after pref set to false");

  info("Test that button is restored when pref is toggled back");
  await SpecialPowers.popPrefEnv();
  Assert.ok(!button.hidden, "Button is visible after pref restored to true");
});

add_task(async function test_buttonOpensSubView() {
  await new CustomizableUITestUtils(window).openMainMenu();
  let subView = PanelMultiView.getViewNode(
    document,
    "appMenu-tabGroupsListView"
  );

  info("Test that clicking the button opens the tab groups subview");
  let viewShown = BrowserTestUtils.waitForEvent(subView, "ViewShown");
  PanelMultiView.getViewNode(document, "appMenu-tab-groups-button").click();
  let event = await viewShown;
  Assert.equal(
    event.target,
    subView,
    "Tab groups subview was shown after clicking button"
  );
  await closeAppMenu();
});

add_task(async function test_rendersOpenGroups() {
  let group1 = await createTestGroup({ label: "Group 1" });
  let group2 = await createTestGroup({ label: "Group 2" });

  let subView = await openTabGroupsSubView();

  let rows = subView.querySelectorAll(".tab-group-row:not([data-saved])");
  Assert.equal(rows.length, 2, "Two open group rows");
  let labels = Array.from(rows).map(
    r => r.querySelector(".tab-group-row-label").textContent
  );
  Assert.ok(labels.includes("Group 1"), "First group is listed");
  Assert.ok(labels.includes("Group 2"), "Second group is listed");

  await closeAppMenu();
  await removeTabGroup(group1);
  await removeTabGroup(group2);
  TabGroupTestUtils.forgetSavedTabGroups();
});

add_task(async function test_rendersSavedGroups() {
  let group = await createTestGroup({ label: "Saved Group" });
  await TabGroupTestUtils.saveAndCloseTabGroup(group);

  let subView = await openTabGroupsSubView();
  let savedRows = subView.querySelectorAll(".tab-group-row[data-saved]");
  Assert.equal(savedRows.length, 1, "One saved group row");

  await closeAppMenu();
  TabGroupTestUtils.forgetSavedTabGroups();
});

// Ensures that the "New Group" button in the list's populated state
// appears and correctly creates a new group containing a new tab.
add_task(async function test_newGroupButton() {
  let group1 = await createTestGroup({ label: "Group 1" });
  let subView = await openTabGroupsSubView();
  let button = subView.querySelector("#tab-groups-list-create-group");
  Assert.ok(
    button,
    "New Group button exists when the tab groups list is populated"
  );

  let group2 = await waitForNewTabGroup(button);

  Assert.notEqual(
    group1,
    group2,
    "newly created group should be different from existing group"
  );
  await removeTabGroup(group1);
  await removeTabGroup(group2);
  TabGroupTestUtils.forgetSavedTabGroups();
});

add_task(async function test_emptyState() {
  let subView = await openTabGroupsSubView();
  let emptyState = subView.querySelector(".tab-groups-list-empty-state");
  Assert.ok(emptyState, "Empty state element is rendered");
  Assert.equal(
    subView.querySelectorAll(".tab-group-row").length,
    0,
    "No group rows rendered in empty state"
  );
  Assert.ok(
    emptyState.querySelector(".tab-groups-list-empty-state-header"),
    "Empty state header is rendered"
  );
  Assert.ok(
    emptyState.querySelector(".tab-groups-list-empty-state-description"),
    "Empty state description is rendered"
  );
  Assert.ok(
    emptyState.querySelector("moz-button"),
    "Empty state button is rendered"
  );
  await closeAppMenu();
});

add_task(async function test_emptyStateButtonCreatesTabGroup() {
  let subView = await openTabGroupsSubView();
  let button = subView.querySelector(".tab-groups-list-empty-state moz-button");

  let tabGroup = await waitForNewTabGroup(button);

  await removeTabGroup(tabGroup);
  TabGroupTestUtils.forgetSavedTabGroups();
});

add_task(async function test_clickOpenGroupActivatesGroup() {
  let group = await createTestGroup({ label: "Select Me" });
  gBrowser.selectedTab = gBrowser.tabs[0];

  let subView = await openTabGroupsSubView();

  let panel = document.getElementById("appMenu-popup");
  let panelHidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
  subView.querySelector(".tab-group-row:not([data-saved])").click();
  await panelHidden;

  Assert.equal(
    gBrowser.selectedTab.group?.id,
    group.id,
    "Tab in the clicked group is selected"
  );

  await removeTabGroup(group);
});

add_task(async function test_clickSavedGroupRestoresGroup() {
  let group = await createTestGroup({ id: "restore-me", label: "Restore Me" });
  await TabGroupTestUtils.saveAndCloseTabGroup(group);
  Assert.ok(!gBrowser.getTabGroupById("restore-me"), "Group is saved/closed");

  let subView = await openTabGroupsSubView();

  let panel = document.getElementById("appMenu-popup");
  let panelHidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
  let groupRestored = BrowserTestUtils.waitForEvent(
    window,
    "SSWindowStateReady"
  );
  subView.querySelector(".tab-group-row[data-saved]").click();
  await Promise.all([panelHidden, groupRestored]);

  let restoredGroup = gBrowser.getTabGroupById("restore-me");
  Assert.equal(
    restoredGroup.name,
    "Restore Me",
    "Restored group has correct name"
  );

  await removeTabGroup(restoredGroup);
  TabGroupTestUtils.forgetSavedTabGroups();
});

add_task(async function test_privateWindowGroups() {
  let normalGroup = await createTestGroup({ label: "Normal Group" });

  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  await createTestGroup({
    label: "Private Group",
    targetWin: privateWin,
  });

  let normalSubView = await openTabGroupsSubView();
  let normalLabels = Array.from(
    normalSubView.querySelectorAll(".tab-group-row-label")
  ).map(el => el.textContent);
  Assert.ok(
    normalLabels.includes("Normal Group"),
    "Normal group is listed in normal window"
  );
  Assert.ok(
    !normalLabels.includes("Private Group"),
    "Private group is not listed in normal window"
  );
  await closeAppMenu();

  let privateSubView = await openTabGroupsSubView(privateWin);
  let privateLabels = Array.from(
    privateSubView.querySelectorAll(".tab-group-row-label")
  ).map(el => el.textContent);
  Assert.ok(
    privateLabels.includes("Private Group"),
    "Private group is listed in private window"
  );
  Assert.ok(
    !privateLabels.includes("Normal Group"),
    "Normal group is not listed in private window"
  );
  await closeAppMenu(privateWin);

  await removeTabGroup(normalGroup);
  await BrowserTestUtils.closeWindow(privateWin);
  TabGroupTestUtils.forgetSavedTabGroups();
});

add_task(async function test_contextMenus() {
  let openGroup = await createTestGroup({ label: "Context Open" });
  let savedGroup = await createTestGroup({ label: "Context Saved" });
  await TabGroupTestUtils.saveAndCloseTabGroup(savedGroup);

  let subView = await openTabGroupsSubView();

  let openRow = subView.querySelector(".tab-group-row:not([data-saved])");
  let openContextMenu = document.getElementById("open-tab-group-context-menu");
  let menuShown = BrowserTestUtils.waitForPopupEvent(openContextMenu, "shown");
  EventUtils.synthesizeMouseAtCenter(
    openRow,
    { type: "contextmenu", button: 2 },
    window
  );
  await menuShown;
  Assert.strictEqual(
    openContextMenu.state,
    "open",
    "open-tab-group-context-menu opened"
  );
  Assert.ok(
    openContextMenu.querySelector("#open-tab-group-context-menu_share").hidden,
    "Share Group item is hidden when content sharing is disabled"
  );
  await closeContextMenu(openContextMenu);

  await SpecialPowers.pushPrefEnv({
    set: [["browser.contentsharing.enabled", true]],
  });
  menuShown = BrowserTestUtils.waitForPopupEvent(openContextMenu, "shown");
  EventUtils.synthesizeMouseAtCenter(
    openRow,
    { type: "contextmenu", button: 2 },
    window
  );
  await menuShown;
  Assert.ok(
    !openContextMenu.querySelector("#open-tab-group-context-menu_share").hidden,
    "Share Group item is shown when content sharing is enabled"
  );
  await closeContextMenu(openContextMenu);
  await SpecialPowers.popPrefEnv();

  let savedRow = subView.querySelector(".tab-group-row[data-saved]");
  let savedContextMenu = document.getElementById(
    "saved-tab-group-context-menu"
  );
  menuShown = BrowserTestUtils.waitForPopupEvent(savedContextMenu, "shown");
  EventUtils.synthesizeMouseAtCenter(
    savedRow,
    { type: "contextmenu", button: 2 },
    window
  );
  await menuShown;
  Assert.strictEqual(
    savedContextMenu.state,
    "open",
    "saved-tab-group-context-menu opened"
  );

  await closeContextMenu(savedContextMenu);
  await closeAppMenu();
  await removeTabGroup(openGroup);
  TabGroupTestUtils.forgetSavedTabGroups();
});

add_task(async function test_shareGroupClosesPanel() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.contentsharing.enabled", true]],
  });
  let openGroup = await createTestGroup({ label: "Context Share" });

  let subView = await openTabGroupsSubView();
  let openRow = subView.querySelector(".tab-group-row:not([data-saved])");
  let openContextMenu = document.getElementById("open-tab-group-context-menu");
  let menuShown = BrowserTestUtils.waitForPopupEvent(openContextMenu, "shown");
  EventUtils.synthesizeMouseAtCenter(
    openRow,
    { type: "contextmenu", button: 2 },
    window
  );
  await menuShown;

  // Stub out the actual share flow; we only want to verify the panel closes.
  let shareStub = sinon
    .stub(ContentSharingUtils, "handleShareTabGroup")
    .resolves();
  let widgetPanel = openRow.closest("panel");
  let panelHidden = BrowserTestUtils.waitForPopupEvent(widgetPanel, "hidden");
  openContextMenu.activateItem(
    openContextMenu.querySelector("#open-tab-group-context-menu_share")
  );
  await panelHidden;

  Assert.equal(
    widgetPanel.state,
    "closed",
    "List all tabs panel is closed when the share dialog opens"
  );
  Assert.ok(
    shareStub.calledOnce,
    "handleShareTabGroup was called for the shared tab group"
  );

  shareStub.restore();
  await removeTabGroup(openGroup);
  TabGroupTestUtils.forgetSavedTabGroups();
  await SpecialPowers.popPrefEnv();
});

// Right-clicking on a row's label makes the popup's triggerNode the label
// element rather than the row button carrying data-tab-group-id, so this
// exercises resolving the tab group from a descendant trigger node.
add_task(async function test_contextMenuDeleteFromRowLabel() {
  let openGroup = await createTestGroup({ label: "Delete Open" });
  let savedGroup = await createTestGroup({ label: "Delete Saved" });
  let savedGroupId = savedGroup.id;
  await TabGroupTestUtils.saveAndCloseTabGroup(savedGroup);

  let subView = await openTabGroupsSubView();

  let openLabel = subView.querySelector(
    ".tab-group-row:not([data-saved]) .tab-group-row-label"
  );
  let openContextMenu = document.getElementById("open-tab-group-context-menu");
  let menuShown = BrowserTestUtils.waitForPopupEvent(openContextMenu, "shown");
  EventUtils.synthesizeMouseAtCenter(
    openLabel,
    { type: "contextmenu", button: 2 },
    window
  );
  await menuShown;
  let menuHidden = BrowserTestUtils.waitForPopupEvent(
    openContextMenu,
    "hidden"
  );
  let panelHidden = BrowserTestUtils.waitForPopupEvent(
    subView.closest("panel"),
    "hidden"
  );
  openContextMenu.activateItem(
    document.getElementById("open-tab-group-context-menu_delete")
  );
  await Promise.all([menuHidden, panelHidden]);
  await TestUtils.waitForCondition(
    () => !gBrowser.getTabGroupById(openGroup.id),
    "open tab group was deleted"
  );
  Assert.ok(
    !gBrowser.getTabGroupById(openGroup.id),
    "open tab group was deleted from the context menu"
  );

  subView = await openTabGroupsSubView();
  let savedLabel = subView.querySelector(
    ".tab-group-row[data-saved] .tab-group-row-label"
  );
  let savedContextMenu = document.getElementById(
    "saved-tab-group-context-menu"
  );
  menuShown = BrowserTestUtils.waitForPopupEvent(savedContextMenu, "shown");
  EventUtils.synthesizeMouseAtCenter(
    savedLabel,
    { type: "contextmenu", button: 2 },
    window
  );
  await menuShown;
  menuHidden = BrowserTestUtils.waitForPopupEvent(savedContextMenu, "hidden");
  panelHidden = BrowserTestUtils.waitForPopupEvent(
    subView.closest("panel"),
    "hidden"
  );
  savedContextMenu.activateItem(
    document.getElementById("saved-tab-group-context-menu_delete")
  );
  await Promise.all([menuHidden, panelHidden]);
  await TestUtils.waitForCondition(
    () => !SessionStore.savedGroups.some(group => group.id == savedGroupId),
    "saved tab group was forgotten"
  );
  Assert.ok(
    !SessionStore.savedGroups.some(group => group.id == savedGroupId),
    "saved tab group was forgotten from the context menu"
  );

  TabGroupTestUtils.forgetSavedTabGroups();
});
