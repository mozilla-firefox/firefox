/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  TabGroupTestUtils: "resource://testing-common/TabGroupTestUtils.sys.mjs",
  TabStateFlusher:
    "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs",
});

// Context menu tests
// ---

function createManyTabs(number, win = window) {
  return Array.from({ length: number }, () => {
    return BrowserTestUtils.addTab(win.gBrowser, "about:blank", {
      skipAnimation: true,
    });
  });
}

async function waitForAndAcceptGroupPanel(actionCallback) {
  let editor = document.getElementById("tab-group-editor");
  let panelShown = BrowserTestUtils.waitForPopupEvent(editor.panel, "shown");
  let done = BrowserTestUtils.waitForEvent(editor, "TabGroupCreateDone");
  await actionCallback();
  await panelShown;
  EventUtils.synthesizeKey("VK_RETURN");
  await done;
}

/**
 * @param {MozTabbrowserTab} tab
 * @param {function(Element?, Element?, Element?):void} callback
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

  const moveTabToNewGroupItem = document.getElementById(
    "context_moveTabToNewGroup"
  );
  const moveTabToGroupItem = document.getElementById("context_moveTabToGroup");
  const ungroupTabItem = document.getElementById("context_ungroupTab");

  let contextMenuHidden = BrowserTestUtils.waitForPopupEvent(
    tabContextMenu,
    "hidden"
  );
  await callback(moveTabToNewGroupItem, moveTabToGroupItem, ungroupTabItem);
  tabContextMenu.hidePopup();
  return contextMenuHidden;
};

/**
 * @param {MozTabbrowserTab} tab
 * @param {function(MozTabbrowserTab):void} callback
 */
async function withNewTabFromTabMenu(tab, callback) {
  await withTabMenu(tab, async () => {
    const newTabPromise = BrowserTestUtils.waitForEvent(document, "TabOpen");
    const newTabToRight = document.getElementById("context_openANewTab");
    newTabToRight.click();
    const { target: newTab } = await newTabPromise;
    await callback(newTab);
    BrowserTestUtils.removeTab(newTab);
  });
}

/*
 * Tests that the context menu options do not appear if the tab group pref is
 * disabled
 */
add_task(async function test_tabGroupTabContextMenuWithoutPref() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.groups.enabled", false]],
  });

  let tab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });

  await withTabMenu(
    tab,
    async (moveTabToNewGroupItem, moveTabToGroupItem, ungroupTabItem) => {
      Assert.ok(
        moveTabToNewGroupItem.hidden,
        "moveTabToNewGroupItem is hidden"
      );
      Assert.ok(moveTabToGroupItem.hidden, "moveTabToGroupItem is hidden");
      Assert.ok(ungroupTabItem.hidden, "ungroupTabItem is hidden");
    }
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

// Context menu tests: "move tab to new group" option
// (i.e. the option that appears in the menu when no other groups exist)
// ---

/*
 * Tests that when no groups exist, if a tab is selected, the "move tab to
 * group" option appears in the context menu, and clicking it moves the tab to
 * a new group
 */
add_task(async function test_tabGroupContextMenuMoveTabToNewGroup() {
  let tab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });

  await waitForAndAcceptGroupPanel(
    async () =>
      await withTabMenu(
        tab,
        async (moveTabToNewGroupItem, moveTabToGroupItem) => {
          Assert.equal(tab.group, null, "tab is not in group");
          Assert.ok(
            !moveTabToNewGroupItem.hidden,
            "moveTabToNewGroupItem is visible"
          );
          Assert.ok(moveTabToGroupItem.hidden, "moveTabToGroupItem is hidden");

          moveTabToNewGroupItem.click();
        }
      )
  );

  Assert.ok(tab.group, "tab is in group");
  Assert.equal(tab.group.label, "", "tab group label is empty");
  Assert.equal(
    gBrowser.selectedTab.group?.id,
    tab.group.id,
    "A tab in the group is selected"
  );

  await removeTabGroup(tab.group);
});

/*
 * Tests that when no groups exist, if multiple tabs are selected and one of
 * the selected tabs has its context menu open, the "move tabs to group" option
 * appears in the context menu, and clicking it moves the tabs to a new group
 */
add_task(async function test_tabGroupContextMenuMoveTabsToNewGroup() {
  const tabs = createManyTabs(3);

  // Click the first tab in our test group to make sure the default tab at the
  // start of the tab strip is deselected
  EventUtils.synthesizeMouseAtCenter(tabs[0], {});

  tabs.forEach(t => {
    EventUtils.synthesizeMouseAtCenter(
      t,
      { ctrlKey: true, metaKey: true },
      window
    );
  });

  let tabToClick = tabs[2];

  await waitForAndAcceptGroupPanel(
    async () =>
      await waitForAndAcceptGroupPanel(
        async () =>
          await withTabMenu(
            tabToClick,
            async (moveTabToNewGroupItem, moveTabToGroupItem) => {
              Assert.ok(
                !moveTabToNewGroupItem.hidden,
                "moveTabToNewGroupItem is visible"
              );
              Assert.ok(
                moveTabToGroupItem.hidden,
                "moveTabToGroupItem is hidden"
              );

              moveTabToNewGroupItem.click();
            }
          )
      )
  );

  let group = tabs[0].group;

  Assert.ok(tabs[0].group, "tab is in group");
  Assert.equal(tabs[0].group.label, "", "tab group label is empty");
  tabs.forEach((t, idx) => {
    Assert.equal(t.group, group, `tabs[${idx}] is in group`);
  });

  await removeTabGroup(group);
});

/*
 * Tests that when no groups exist, if a tab is selected and a tab that is
 * *not* selected has its context menu open, the "move tab to group" option
 * appears in the context menu, and clicking it moves the *context menu* tab to
 * the group, not the selected tab
 */
add_task(
  async function test_tabGroupContextMenuMoveTabToNewGroupWhileAnotherSelected() {
    let tab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
      skipAnimation: true,
    });
    let otherTab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
      skipAnimation: true,
    });

    EventUtils.synthesizeMouseAtCenter(otherTab, {});

    await waitForAndAcceptGroupPanel(
      async () =>
        await withTabMenu(
          tab,
          async (moveTabToNewGroupItem, moveTabToGroupItem) => {
            Assert.equal(
              gBrowser.selectedTabs.includes(TabContextMenu.contextTab),
              false,
              "context menu tab is not selected"
            );
            Assert.ok(
              !moveTabToNewGroupItem.hidden,
              "moveTabToNewGroupItem is visible"
            );
            Assert.ok(
              moveTabToGroupItem.hidden,
              "moveTabToGroupItem is hidden"
            );

            moveTabToNewGroupItem.click();
          }
        )
    );

    Assert.ok(tab.group, "tab is in group");
    Assert.equal(otherTab.group, null, "otherTab is not in group");

    await removeTabGroup(tab.group);
    BrowserTestUtils.removeTab(otherTab);
  }
);

/*
 * Tests that when no groups exist, if multiple tabs are selected and a tab
 * that is *not* selected has its context menu open, the "move tabs to group"
 * option appears in the context menu, and clicking it moves the *context menu*
 * tab to the group, not the selected tabs
 */
add_task(
  async function test_tabGroupContextMenuMoveTabToNewGroupWhileOthersSelected() {
    let tab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
      skipAnimation: true,
    });

    const otherTabs = createManyTabs(3);

    otherTabs.forEach(t => {
      EventUtils.synthesizeMouseAtCenter(
        t,
        { ctrlKey: true, metaKey: true },
        window
      );
    });

    await waitForAndAcceptGroupPanel(
      async () =>
        await withTabMenu(
          tab,
          async (moveTabToNewGroupItem, moveTabToGroupItem) => {
            Assert.equal(
              gBrowser.selectedTabs.includes(TabContextMenu.contextTab),
              false,
              "context menu tab is not selected"
            );
            Assert.ok(
              !moveTabToNewGroupItem.hidden,
              "moveTabToNewGroupItem is visible"
            );
            Assert.ok(
              moveTabToGroupItem.hidden,
              "moveTabToGroupItem is hidden"
            );

            moveTabToNewGroupItem.click();
          }
        )
    );

    Assert.ok(tab.group, "tab is in group");

    otherTabs.forEach((t, idx) => {
      Assert.equal(t.group, null, `otherTab[${idx}] is not in group`);
    });

    await removeTabGroup(tab.group);
    otherTabs.forEach(t => {
      BrowserTestUtils.removeTab(t);
    });
  }
);

// Context menu tests: "move tab to group" option
// (i.e. the option that appears in the menu when other groups already exist)
// ---

/*
 * Tests that when groups exist, the "move tab to group" menu option is visible
 * and is correctly populated with the group list
 */
add_task(async function test_tabGroupContextMenuMoveTabToGroupBasics() {
  let tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });
  let group1 = gBrowser.addTabGroup([tab1], {
    color: "red",
    label: "Test group with label",
  });

  // Make sure the first and second groups have different lastSeenActive times.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 1));

  let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });
  let group2 = gBrowser.addTabGroup([tab2], { color: "blue", label: "" });

  Assert.greater(
    group2.lastSeenActive,
    group1.lastSeenActive,
    "last created group should have higher lastSeenActive time"
  );

  let tabToClick = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });

  await withTabMenu(
    tabToClick,
    async (moveTabToNewGroupItem, moveTabToGroupItem) => {
      Assert.ok(
        moveTabToNewGroupItem.hidden,
        "moveTabToNewGroupItem is hidden"
      );
      Assert.ok(!moveTabToGroupItem.hidden, "moveTabToGroupItem is visible");

      const submenu = moveTabToGroupItem.querySelector(
        "#context_moveTabToGroupPopupMenu"
      ).children;

      Assert.equal(
        submenu[0].label,
        "New Group",
        "First item in the list is the 'New Group' item"
      );
      Assert.equal(
        submenu[1].tagName,
        "menuseparator",
        "Second item in the list is a menuseparator"
      );
      // Items 0 and 1 are the "new group" item and a separator respectively
      // Note that groups should appear in order of most recently used to least
      const group2Item = submenu[2];
      Assert.equal(
        group2Item.getAttribute("tab-group-id"),
        group2.id,
        "first group in list is group2"
      );
      Assert.equal(
        group2Item.getAttribute("label"),
        "Unnamed group",
        "group2 menu item has correct label"
      );
      Assert.ok(
        Array.from(group2Item.classList).includes("tab-group-icon"),
        "Closed group icon is presented in solid form"
      );
      Assert.ok(
        group2Item.style
          .getPropertyValue("--tab-group-color")
          .includes("--tab-group-blue"),
        "group2 menu item chicklet has correct color"
      );
      Assert.ok(
        group2Item.style
          .getPropertyValue("--tab-group-color-invert")
          .includes("--tab-group-blue-invert"),
        "group2 menu item chicklet has correct inverted color"
      );

      const group1Item = submenu[3];
      Assert.equal(
        group1Item.getAttribute("tab-group-id"),
        group1.id,
        "second group in list is group1"
      );
      Assert.equal(
        group1Item.getAttribute("label"),
        "Test group with label",
        "group1 menu item has correct label"
      );
      Assert.ok(
        group1Item.style
          .getPropertyValue("--tab-group-color")
          .includes("--tab-group-red"),
        "group1 menu item chicklet has correct color"
      );
      Assert.ok(
        group1Item.style
          .getPropertyValue("--tab-group-color-invert")
          .includes("--tab-group-red-invert"),
        "group1 menu item chicklet has correct inverted color"
      );

      Assert.equal(
        submenu[4].tagName,
        "menuseparator",
        "The item immediately after the last open tab group is a menuseparator"
      );

      Assert.equal(
        submenu[5].label,
        "Closed Groups",
        "Final item in the list is the closed groups dropdown"
      );
      Assert.equal(
        submenu[5].disabled,
        true,
        "Closed groups dropdown is disabled when there are no saved groups"
      );
    }
  );

  await removeTabGroup(group1);
  await removeTabGroup(group2);
  BrowserTestUtils.removeTab(tabToClick);
});

/*
 * Tests that the "move tab to group > new group" option creates a new group and moves the tab to it
 */
add_task(async function test_tabGroupContextMenuMoveTabToGroupNewGroup() {
  let otherTab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });
  let otherGroup = gBrowser.addTabGroup([otherTab]);

  let tab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });

  await waitForAndAcceptGroupPanel(
    async () =>
      await withTabMenu(tab, async (_, moveTabToGroupItem) => {
        moveTabToGroupItem
          .querySelector("#context_moveTabToGroupNewGroup")
          .click();
      })
  );

  Assert.ok(tab.group, "tab is in group");
  Assert.notEqual(
    tab.group.id,
    otherGroup.id,
    "tab is not in the original group"
  );

  await removeTabGroup(otherGroup);
  await removeTabGroup(tab.group);
});

/**
 * Ensure group is positioned correctly when a pinned tab is grouped
 */
add_task(async function test_tabGroupContextMenuMovePinnedTabToNewGroup() {
  let pinnedTab = await addTab("about:blank");
  let pinnedUngroupedTab = await addTab("about:blank");
  gBrowser.pinTab(pinnedTab);
  gBrowser.pinTab(pinnedUngroupedTab);
  await waitForAndAcceptGroupPanel(
    async () =>
      await withTabMenu(pinnedTab, async (_, moveTabToGroupItem) => {
        moveTabToGroupItem
          .querySelector("#context_moveTabToGroupNewGroup")
          .click();
      })
  );
  Assert.ok(!pinnedTab.pinned, "first pinned tab is no longer pinned");
  Assert.ok(pinnedTab.group, "first pinned tab is grouped");
  Assert.greater(
    pinnedTab._tPos,
    pinnedUngroupedTab._tPos,
    "pinned tab's group appears after the list of pinned tabs"
  );
  await removeTabGroup(pinnedTab.group);
  BrowserTestUtils.removeTab(pinnedUngroupedTab);
});

/*
 * Tests that the "move tab to group > [group name]" option moves a tab to the selected group
 */
add_task(async function test_tabGroupContextMenuMoveTabToExistingGroup() {
  let otherTab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });
  let group = gBrowser.addTabGroup([otherTab]);

  let tab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });

  await withTabMenu(tab, async (_, moveTabToGroupItem) => {
    moveTabToGroupItem.querySelector(`[tab-group-id="${group.id}"]`).click();
  });

  Assert.ok(tab.group, "tab is in group");
  Assert.equal(tab.group.id, group.id, "tab is in the original group");

  await removeTabGroup(group);
});

/*
 * Tests that the "move tab to group > [group name]" option moves a splitview to the selected
 * group in another window
 */
add_task(
  async function test_tabGroupContextMenuMoveSplitViewToExistingGroupInOtherWindow() {
    const tab1 = await addTab();
    const tab2 = await addTab();
    gBrowser.addTabSplitView([tab1, tab2]);

    const win = await BrowserTestUtils.openNewBrowserWindow();
    let otherTab = BrowserTestUtils.addTab(win.gBrowser, "about:blank", {
      skipAnimation: true,
    });
    let group = win.gBrowser.addTabGroup([otherTab]);
    Assert.ok(
      !group.tabsAndSplitViews.some(
        node => node.tagName == "tab-split-view-wrapper"
      ),
      "No splitview is currently in the group"
    );

    await withTabMenu(tab1, async (_, moveTabToGroupItem) => {
      moveTabToGroupItem.querySelector(`[tab-group-id="${group.id}"]`).click();
    });

    Assert.ok(
      group.tabsAndSplitViews.some(
        node => node.tagName == "tab-split-view-wrapper"
      ),
      "Splitview has been moved to group in other window"
    );

    await removeTabGroup(group);
    await BrowserTestUtils.closeWindow(win);
  }
);

/*
 * Same as above, but for groups in different windows
 */
add_task(
  async function test_tabGroupContextMenuMoveTabToExistingGroupInDifferentWindow() {
    let otherWindow = await BrowserTestUtils.openNewBrowserWindow();
    let otherTab = await addTabTo(otherWindow.gBrowser, "about:blank", {
      skipAnimation: true,
    });
    let group = otherWindow.gBrowser.addTabGroup([otherTab]);
    let tab = await addTab("about:blank", {
      skipAnimation: true,
    });

    let windowActivated = BrowserTestUtils.waitForEvent(window, "activate");
    window.focus();
    await windowActivated;
    Assert.equal(
      BrowserWindowTracker.getTopWindow(),
      window,
      "current window is active before moving group to another window"
    );

    let tabGrouped = BrowserTestUtils.waitForEvent(group, "TabGrouped");
    let otherWindowActivated = BrowserTestUtils.waitForEvent(
      otherWindow,
      "activate"
    );
    await withTabMenu(tab, async (_, moveTabToGroupItem) => {
      moveTabToGroupItem.querySelector(`[tab-group-id="${group.id}"]`).click();
    });
    await Promise.allSettled([tabGrouped, otherWindowActivated]);
    Assert.equal(group.tabs.length, 2, "group has 2 tabs");
    Assert.equal(
      BrowserWindowTracker.getTopWindow(),
      otherWindow,
      "moving group activates target window"
    );

    await BrowserTestUtils.closeWindow(otherWindow);
  }
);

/*
 * Tests that when groups exist, and the context menu tab has a group,
 * that group does not exist in the context menu list
 */
add_task(
  async function test_tabGroupContextMenuMoveTabToGroupContextMenuTabNotInList() {
    let tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank", {
      skipAnimation: true,
    });
    let group1 = gBrowser.addTabGroup([tab1]);
    let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank", {
      skipAnimation: true,
    });
    let group2 = gBrowser.addTabGroup([tab2]);

    await withTabMenu(tab2, async (_, moveTabToGroupItem) => {
      const submenu = moveTabToGroupItem.querySelector(
        "#context_moveTabToGroupPopupMenu"
      ).children;

      // Accounting for the existence of the "new group" and menuseparator elements
      const numberOfStaticElements = 4;
      Assert.equal(
        submenu.length,
        numberOfStaticElements + 1,
        "only one tab group exists in the list"
      );
      Assert.equal(
        submenu[2].getAttribute("tab-group-id"),
        group1.id,
        "tab group in the list is not the context menu tab's group"
      );
    });

    await removeTabGroup(group1);
    await removeTabGroup(group2);
  }
);

/*
 * Tests that when only one group exists, and the context menu tab is in the group,
 * the condensed "move tab to new group" menu item is shown in place of the submenu variant
 */
add_task(
  async function test_tabGroupContextMenuMoveTabToGroupOnlyOneGroupIsSelectedGroup() {
    let tab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
      skipAnimation: true,
    });
    let group = gBrowser.addTabGroup([tab]);

    await withTabMenu(
      tab,
      async (moveTabToNewGroupItem, moveTabToGroupItem) => {
        Assert.ok(
          !moveTabToNewGroupItem.hidden,
          "moveTabToNewGroupItem is visible"
        );
        Assert.ok(moveTabToGroupItem.hidden, "moveTabToGroupItem is hidden");
      }
    );

    await removeTabGroup(group);
  }
);

/*
 * Tests that when many groups exist, if many tabs are selected and the
 * selected tabs belong to different groups or are ungrouped, all tab groups
 * appear in the context menu list
 */
add_task(
  async function test_tabGroupContextMenuManySelectedTabsFromManyGroups() {
    const tabs = createManyTabs(3);

    let group1 = gBrowser.addTabGroup([tabs[0]]);
    let group2 = gBrowser.addTabGroup([tabs[1]]);

    tabs.forEach(tab => {
      EventUtils.synthesizeMouseAtCenter(
        tab,
        { ctrlKey: true, metaKey: true },
        window
      );
    });

    const tabToClick = tabs[2];

    await withTabMenu(tabToClick, async (_, moveTabToGroupItem) => {
      const submenu = moveTabToGroupItem.querySelector(
        "#context_moveTabToGroupPopupMenu"
      ).children;

      const tabGroupIds = Array.from(submenu).map(item =>
        item.getAttribute("tab-group-id")
      );

      Assert.ok(
        tabGroupIds.includes(group1.getAttribute("id")),
        "group1 is in context menu list"
      );
      Assert.ok(
        tabGroupIds.includes(group2.getAttribute("id")),
        "group2 is in context menu list"
      );
    });

    await removeTabGroup(group1);
    await removeTabGroup(group2);
    BrowserTestUtils.removeTab(tabToClick);
  }
);

/*
 * Tests that when many groups exist, if many tabs are selected and all the
 * tabs belong to the same group, that group does not appear in the context
 * menu list
 */
add_task(
  async function test_tabGroupContextMenuManySelectedTabsFromSameGroup() {
    const tabsToSelect = createManyTabs(3);
    let selectedTabGroup = gBrowser.addTabGroup(tabsToSelect);
    let otherTab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
      skipAnimation: true,
    });
    let otherGroup = gBrowser.addTabGroup([otherTab]);

    // Click the first tab in our test group to make sure the default tab at the
    // start of the tab strip is deselected
    // This is broken on tabs within tab groups ...
    EventUtils.synthesizeMouseAtCenter(tabsToSelect[0], {});

    tabsToSelect.forEach(tab => {
      EventUtils.synthesizeMouseAtCenter(
        tab,
        { ctrlKey: true, metaKey: true },
        window
      );
    });

    await withTabMenu(tabsToSelect[2], async (_, moveTabToGroupItem) => {
      const submenu = moveTabToGroupItem.querySelector(
        "#context_moveTabToGroupPopupMenu"
      ).children;

      const tabGroupIds = Array.from(submenu).map(item =>
        item.getAttribute("tab-group-id")
      );

      Assert.ok(
        !tabGroupIds.includes(selectedTabGroup.getAttribute("id")),
        "group with selected tabs is not in context menu list"
      );
    });

    let ungroupedTab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
      skipAnimation: true,
    });
    EventUtils.synthesizeMouseAtCenter(
      ungroupedTab,
      { ctrlKey: true, metaKey: true },
      window
    );
    await withTabMenu(tabsToSelect[2], async (_, moveTabToGroupItem) => {
      const submenu = moveTabToGroupItem.querySelector(
        "#context_moveTabToGroupPopupMenu"
      ).children;

      const tabGroupIds = Array.from(submenu).map(item =>
        item.getAttribute("tab-group-id")
      );

      Assert.ok(
        tabGroupIds.includes(selectedTabGroup.getAttribute("id")),
        "group with selected tabs is in context menu list since one of the selected tabs is ungrouped"
      );
    });

    await removeTabGroup(selectedTabGroup);
    await removeTabGroup(otherGroup);
    BrowserTestUtils.removeTab(ungroupedTab);
  }
);

/*
 * Tests that if there are saved groups and no open groups, the "move tab to
 * group" menu appears with only saved groups in it
 */
add_task(async function test_tabGroupContextMenuSavedGroupsAndNoOpenGroups() {
  let tab = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  let group = gBrowser.addTabGroup([tab], {
    label: "Test group",
  });
  let expectedGroupLabel = group.label;
  let expectedGroupId = group.id;

  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  await lazy.TabGroupTestUtils.saveAndCloseTabGroup(group);
  Assert.equal(
    SessionStore.getSavedTabGroups().length,
    1,
    "The group was saved"
  );

  tab = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  gBrowser.selectedTab = tab;

  // TODO remove once bug1973996 is resolved
  await lazy.TabStateFlusher.flush(tab.linkedBrowser);

  await withTabMenu(tab, async (_, moveTabToGroupItem) => {
    const firstLevelMenu = moveTabToGroupItem.querySelector(
      "#context_moveTabToGroupPopupMenu"
    ).children;

    Assert.equal(firstLevelMenu.length, 4, "Menu has four items");
    Assert.equal(
      firstLevelMenu[0].label,
      "New Group",
      "First item in the list is the 'New Group' item"
    );
    Assert.equal(
      firstLevelMenu[1].tagName,
      "menuseparator",
      "Second item in the list is the upper menuseparator"
    );
    Assert.equal(
      firstLevelMenu[2].tagName,
      "menuseparator",
      "Third item in the list is the lower menuseparator"
    );
    Assert.ok(firstLevelMenu[2].hidden, "Lower menuseparator is hidden");
    Assert.equal(
      firstLevelMenu[3].label,
      "Closed Groups",
      "Fourth item in the list is the closed groups dropdown"
    );
    Assert.ok(!firstLevelMenu[3].disabled, "Closed groups dropdown is enabled");

    const secondLevelMenu =
      firstLevelMenu[3].querySelector("menupopup").children;
    Assert.equal(secondLevelMenu.length, 1, "Closed groups menu has 1 item");

    const savedGroupMenuItem = secondLevelMenu[0];
    Assert.equal(
      savedGroupMenuItem.getAttribute("tab-group-id"),
      expectedGroupId,
      "Saved group item has correct id"
    );
    Assert.equal(
      savedGroupMenuItem.label,
      expectedGroupLabel,
      "Saved group item has correct label"
    );
    Assert.ok(
      Array.from(savedGroupMenuItem.classList).includes(
        "tab-group-icon-closed"
      ),
      "Saved group icon is presented in outlined form"
    );
    Assert.ok(
      savedGroupMenuItem.style
        .getPropertyValue("--tab-group-color")
        .includes("--tab-group-blue"),
      "Saved group icon has correct color"
    );
    Assert.ok(
      savedGroupMenuItem.style
        .getPropertyValue("--tab-group-color-invert")
        .includes("--tab-group-blue-invert"),
      "Saved group icon has correct inverted color"
    );
  });

  BrowserTestUtils.removeTab(tab);
  lazy.TabGroupTestUtils.forgetSavedTabGroups();
});

/*
 * Tests that if there are both saved groups and open groups, the "move tab to
 * group" menu appears with both types of groups in it, and are separated correctly
 */
add_task(async function test_tabGroupContextMenuSavedGroupsAndOpenGroups() {
  let openTab = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  let openGroup = gBrowser.addTabGroup([openTab], {
    label: "Test group",
  });

  let savedTab = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  let savedGroup = gBrowser.addTabGroup([savedTab], {
    label: "Test group",
  });

  let tabToSelect = BrowserTestUtils.addTab(gBrowser, "https://example.com");

  await Promise.allSettled([
    BrowserTestUtils.browserLoaded(openTab.linkedBrowser),
    BrowserTestUtils.browserLoaded(savedTab.linkedBrowser),
    BrowserTestUtils.browserLoaded(tabToSelect.linkedBrowser),
  ]);

  await lazy.TabGroupTestUtils.saveAndCloseTabGroup(savedGroup);
  Assert.equal(
    SessionStore.getSavedTabGroups().length,
    1,
    "The group was saved"
  );

  // TODO remove once bug1973996 is resolved
  await lazy.TabStateFlusher.flush(tabToSelect.linkedBrowser);

  await withTabMenu(tabToSelect, async (_, moveTabToGroupItem) => {
    const firstLevelMenu = moveTabToGroupItem.querySelector(
      "#context_moveTabToGroupPopupMenu"
    ).children;

    Assert.equal(firstLevelMenu.length, 5, "Menu has five items");

    Assert.equal(
      firstLevelMenu[0].label,
      "New Group",
      "First item in the list is the 'New Group' item"
    );
    Assert.equal(
      firstLevelMenu[1].tagName,
      "menuseparator",
      "Second item in the list is the upper menuseparator"
    );
    Assert.equal(
      firstLevelMenu[2].getAttribute("tab-group-id"),
      openGroup.id,
      "Third item in the list is the open group"
    );
    Assert.equal(
      firstLevelMenu[3].tagName,
      "menuseparator",
      "Fourth item in the list is the lower menuseparator"
    );
    Assert.ok(!firstLevelMenu[3].hidden, "Lower menuseparator is visible");
    Assert.equal(
      firstLevelMenu[4].label,
      "Closed Groups",
      "Fifth item in the list is the closed groups dropdown"
    );
    Assert.ok(!firstLevelMenu[4].disabled, "Saved groups dropdown is enabled");

    const secondLevelMenu =
      firstLevelMenu[4].querySelector("menupopup").children;
    Assert.equal(secondLevelMenu.length, 1, "Saved groups menu has 1 item");
  });

  await lazy.TabGroupTestUtils.removeTabGroup(openGroup);
  BrowserTestUtils.removeTab(tabToSelect);
  lazy.TabGroupTestUtils.forgetSavedTabGroups();
});

/*
 * Tests that if the context tab is has content that is not considered "worth
 * saving" from the point of view of SessionStore, and no other group is open,
 * the "add tab to group" dropdown does not appear, even when a saved group exists
 */
add_task(
  async function test_tabGroupContextMenuSavedGroupsDisabledIfNotWorthSaving() {
    let savedTab = BrowserTestUtils.addTab(gBrowser, "https://example.com");
    let savedGroup = gBrowser.addTabGroup([savedTab], {
      label: "Test group",
    });

    let contextTab = BrowserTestUtils.addTab(gBrowser, "about:blank");

    await Promise.allSettled([
      BrowserTestUtils.browserLoaded(savedTab.linkedBrowser),
      BrowserTestUtils.browserLoaded(contextTab.linkedBrowser, {
        wantLoad: "about:blank",
      }),
    ]);

    await lazy.TabGroupTestUtils.saveAndCloseTabGroup(savedGroup);
    Assert.equal(
      SessionStore.getSavedTabGroups().length,
      1,
      "The group was saved"
    );

    // TODO remove once bug1973996 is resolved
    await lazy.TabStateFlusher.flush(contextTab.linkedBrowser);

    await withTabMenu(
      contextTab,
      async (moveTabToNewGroupItem, moveTabToGroupItem) => {
        Assert.ok(
          !moveTabToNewGroupItem.hidden,
          "moveTabToNewGroupItem is visible"
        );
        Assert.ok(moveTabToGroupItem.hidden, "moveTabToGroupItem is hidden");
      }
    );

    BrowserTestUtils.removeTab(contextTab);
    lazy.TabGroupTestUtils.forgetSavedTabGroups();
  }
);

/*
 * Tests that if the context tab is has content that is not considered "worth
 * saving" from the point of view of SessionStore, and another group is open,
 * the saved group options are disabled, even when a saved group exists
 */
add_task(
  async function test_tabGroupContextMenuSavedGroupsDisabledIfNotWorthSavingWithOpenGroup() {
    let openTab = BrowserTestUtils.addTab(gBrowser, "https://example.com");
    let openGroup = gBrowser.addTabGroup([openTab], {
      label: "Test group",
    });

    let savedTab = BrowserTestUtils.addTab(gBrowser, "https://example.com");
    let savedGroup = gBrowser.addTabGroup([savedTab], {
      label: "Test group",
    });

    let contextTab = BrowserTestUtils.addTab(gBrowser, "about:blank");

    await Promise.allSettled([
      BrowserTestUtils.browserLoaded(openTab.linkedBrowser),
      BrowserTestUtils.browserLoaded(savedTab.linkedBrowser),
      BrowserTestUtils.browserLoaded(contextTab.linkedBrowser, {
        wantLoad: "about:blank",
      }),
    ]);

    await lazy.TabGroupTestUtils.saveAndCloseTabGroup(savedGroup);
    Assert.equal(
      SessionStore.getSavedTabGroups().length,
      1,
      "The group was saved"
    );

    // TODO remove once bug1973996 is resolved
    await lazy.TabStateFlusher.flush(contextTab.linkedBrowser);

    await withTabMenu(contextTab, async (_, moveTabToGroupItem) => {
      const firstLevelMenu = moveTabToGroupItem.querySelector(
        "#context_moveTabToGroupPopupMenu"
      ).children;

      Assert.equal(firstLevelMenu.length, 5, "Menu has five items");
      Assert.equal(
        firstLevelMenu[4].label,
        "Closed Groups",
        "Last item in the list is the saved groups dropdown"
      );
      Assert.ok(
        firstLevelMenu[4].disabled,
        "Saved groups dropdown is disabled"
      );
    });

    await lazy.TabGroupTestUtils.removeTabGroup(openGroup);
    BrowserTestUtils.removeTab(contextTab);
    lazy.TabGroupTestUtils.forgetSavedTabGroups();
  }
);

/*
 * Tests that adding a tab to a saved group correctly adds the tab to the saved
 * group and removes the tab from the window, and that the saved group can be
 * correctly restored
 */
add_task(async function test_tabGroupContextMenuSaveGroupAndRestore() {
  let savedTab = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  let savedGroup = gBrowser.addTabGroup([savedTab], {
    label: "Test group",
  });
  let savedGroupId = savedGroup.id;

  await BrowserTestUtils.browserLoaded(savedTab.linkedBrowser);
  await lazy.TabGroupTestUtils.saveAndCloseTabGroup(savedGroup);

  let sessionStoreGroups = SessionStore.getSavedTabGroups();
  Assert.equal(sessionStoreGroups.length, 1, "The group was saved");
  Assert.equal(
    sessionStoreGroups[0].tabs.length,
    1,
    "There is one tab in the group"
  );

  let tabToAdd = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  await BrowserTestUtils.browserLoaded(tabToAdd.linkedBrowser);
  gBrowser.selectedTab = tabToAdd;

  // TODO remove once bug1973996 is resolved
  await lazy.TabStateFlusher.flush(tabToAdd.linkedBrowser);

  await withTabMenu(tabToAdd, async (_, moveTabToGroupItem) => {
    const firstLevelMenu = moveTabToGroupItem.querySelector(
      "#context_moveTabToGroupPopupMenu"
    ).children;
    const secondLevelMenu =
      firstLevelMenu[3].querySelector("menupopup").children;
    const savedGroupMenuItem = secondLevelMenu[0];

    let tabClosePromise = BrowserTestUtils.waitForEvent(tabToAdd, "TabClose");
    savedGroupMenuItem.click();
    await tabClosePromise;
  });

  sessionStoreGroups = SessionStore.getSavedTabGroups();
  Assert.equal(sessionStoreGroups.length, 1, "Only one group exists");
  Assert.equal(
    sessionStoreGroups[0].tabs.length,
    2,
    "The tab was added to the group"
  );

  let restorePromise = BrowserTestUtils.waitForEvent(
    window,
    "SSWindowStateReady"
  );
  SessionStore.openSavedTabGroup(savedGroupId, window);
  await restorePromise;

  Assert.equal(
    gBrowser.tabGroups.length,
    1,
    "One tab group exists on the tab strip"
  );
  Assert.equal(
    gBrowser.tabGroups[0].id,
    savedGroupId,
    "The tab group is the restored tab group"
  );
  Assert.equal(
    gBrowser.tabGroups[0].tabs.length,
    2,
    "Restored tab group has two tabs"
  );

  await removeTabGroup(gBrowser.tabGroups[0]);
  lazy.TabGroupTestUtils.forgetSavedTabGroups();
});

/*
 * Tests that if a multiselection of tabs is added to a saved group, and only
 * some of those tabs are considered "worth saving" from the point of view of
 * SessionStore, all tabs are removed from the window, but only the tabs that
 * are worth saving are added to the group
 */
add_task(
  async function test_tabGroupContextMenuSaveGroupMultiselectionSomeWorthSaving() {
    let savedTab = BrowserTestUtils.addTab(gBrowser, "https://example.com");
    let savedGroup = gBrowser.addTabGroup([savedTab], {
      label: "Test group",
    });

    await BrowserTestUtils.browserLoaded(savedTab.linkedBrowser);
    await lazy.TabGroupTestUtils.saveAndCloseTabGroup(savedGroup);

    let sessionStoreGroups = SessionStore.getSavedTabGroups();
    Assert.equal(sessionStoreGroups.length, 1, "The group was saved");
    Assert.equal(
      sessionStoreGroups[0].tabs.length,
      1,
      "There is one tab in the group"
    );

    const urls = [
      "https://example.com/",
      "https://example.com/",
      "about:blank",
      "about:blank",
    ];
    const tabsToAdd = urls.map(url => BrowserTestUtils.addTab(gBrowser, url));
    await Promise.allSettled(
      tabsToAdd.map((tab, i) =>
        BrowserTestUtils.browserLoaded(tab.linkedBrowser, { wantLoad: urls[i] })
      )
    );

    gBrowser.selectedTabs = tabsToAdd;

    // TODO remove once bug1973996 is resolved
    await Promise.allSettled(
      tabsToAdd.map(tab => lazy.TabStateFlusher.flush(tab.linkedBrowser))
    );

    await withTabMenu(tabsToAdd[0], async (_, moveTabToGroupItem) => {
      const firstLevelMenu = moveTabToGroupItem.querySelector(
        "#context_moveTabToGroupPopupMenu"
      ).children;
      const secondLevelMenu =
        firstLevelMenu[3].querySelector("menupopup").children;
      const savedGroupMenuItem = secondLevelMenu[0];

      let tabClosePromises = tabsToAdd.map(tab =>
        BrowserTestUtils.waitForEvent(tab, "TabClose")
      );
      savedGroupMenuItem.click();
      await Promise.allSettled(tabClosePromises);
    });

    sessionStoreGroups = SessionStore.getSavedTabGroups();
    Assert.equal(sessionStoreGroups.length, 1, "Only one group exists");
    Assert.equal(
      sessionStoreGroups[0].tabs.length,
      3,
      "Two tabs were added to the group"
    );

    // Use waitForCondition instead of assert because the TabClose event fires before the tab is fully removed from the DOM
    await TestUtils.waitForCondition(
      () => gBrowser.tabs.length == 1,
      "Only one tab remains on the tab strip"
    );

    lazy.TabGroupTestUtils.forgetSavedTabGroups();
  }
);

// Context menu tests: "remove from group" option
// ---

/* Tests that if no groups exist within the selection, the "remove from group"
 * option does not exist
 */
add_task(async function test_removeFromGroupHiddenIfNoGroupInSelection() {
  let unrelatedGroupedTab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });
  let unrelatedGroup = gBrowser.addTabGroup([unrelatedGroupedTab]);

  let tab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });

  await withTabMenu(tab, async (_m1, _m2, ungroupTabItem) => {
    Assert.ok(ungroupTabItem.hidden, "ungroupTabItem is hidden");
  });

  BrowserTestUtils.removeTab(tab);
  await removeTabGroup(unrelatedGroup);
});

/* Tests that if a single tab is selected and that tab is part of a group, the
 * "remove from group" option exists and clicking the item removes the tab from
 * the group
 */
add_task(async function test_removeFromGroupForSingleTab() {
  const tabs = createManyTabs(3);
  let group = gBrowser.addTabGroup(tabs);
  let extraTab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });
  let tabToClick = tabs[1];

  Assert.equal(tabToClick.group, group, "tab is in group");

  await withTabMenu(tabToClick, async (_m1, _m2, ungroupTabItem) => {
    Assert.ok(!ungroupTabItem.hidden, "ungroupTabItem is visible");

    ungroupTabItem.click();
  });

  Assert.ok(!tabToClick.group, "tab is no longer in group");
  Assert.equal(
    gBrowser.tabs[3],
    tabToClick,
    "tab has been moved just outside the group in the tab strip"
  );

  await removeTabGroup(group);
  BrowserTestUtils.removeTab(tabToClick);
  BrowserTestUtils.removeTab(extraTab);
});

/* Tests that if many tabs are selected and at least some of those tabs are
 * part of a group, the "remove from group" option exists and clicking the item
 * removes all tabs from their groups
 */
add_task(async function test_removeFromGroupForMultipleTabs() {
  // initial tab strip: [group1, group1, group1, none, none, group2, group2, none, group3, none]
  let tabs = createManyTabs(10);
  [tabs[0], tabs[1], tabs[2]].forEach(t => {
    gBrowser.addToMultiSelectedTabs(t);
    ok(t.multiselected, "added tab to mutliselection");
  });
  gBrowser.addTabGroup([tabs[0], tabs[1], tabs[2]], { insertBefore: tabs[0] });
  [tabs[0], tabs[1], tabs[2]].forEach(t => {
    ok(!t.multiselected, "tab no longer multiselected after adding to group");
  });
  gBrowser.addTabGroup([tabs[5], tabs[6]], { insertBefore: tabs[5] });
  gBrowser.addTabGroup([tabs[8]], { insertBefore: tabs[8] });

  // Click the first tab in our test group to make sure the default tab at the
  // start of the tab strip is deselected
  EventUtils.synthesizeMouseAtCenter(tabs[1], {});

  // select a few tabs, both in and out of groups
  [tabs[3], tabs[6], tabs[8]].forEach(t => {
    gBrowser.addToMultiSelectedTabs(t);
  });

  let tabToClick = tabs[3];

  await withTabMenu(tabToClick, async (_m1, _m2, ungroupTabItem) => {
    Assert.ok(!ungroupTabItem.hidden, "ungroupTabItem is visible");

    ungroupTabItem.click();
  });

  Assert.ok(!tabs[1].group, "group1 tab is no longer in group");
  Assert.ok(!tabs[6].group, "group2 tab is no longer in group");
  Assert.ok(!tabs[8].group, "group3 tab is no longer in group");

  Assert.equal(
    tabs[1],
    gBrowser.tabs[3],
    "ungrouped tab from group1 is adjacent to group1"
  );
  Assert.equal(
    tabs[6],
    gBrowser.tabs[7],
    "ungrouped tab from group2 has not changed position"
  );
  Assert.equal(
    tabs[8],
    gBrowser.tabs[9],
    "ungrouped tab from group3 has not changed position"
  );

  tabs.forEach(t => {
    BrowserTestUtils.removeTab(t);
  });
});

/* Tests that if a selection containing both regular tabs and splitview tabs is
 * removed from a group, the splitview stays intact (its tabs remain grouped in
 * the same splitview after being moved out of the tab group).
 */
add_task(async function test_removeFromGroupKeepsSplitViewIntact() {
  const normalTab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });
  const splitTab1 = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });
  const splitTab2 = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });

  const splitview = gBrowser.addTabSplitView([splitTab1, splitTab2]);
  const group = gBrowser.addTabGroup([normalTab, splitview]);

  Assert.equal(group.tabs.length, 3, "group has 3 tabs");
  Assert.equal(normalTab.group, group, "normalTab is in the group");
  Assert.equal(splitTab1.group, group, "splitTab1 is in the group");
  Assert.equal(splitTab2.group, group, "splitTab2 is in the group");

  // Multi-select normalTab and one splitview tab (a mixed regular + splitview
  // selection).
  await BrowserTestUtils.switchTab(gBrowser, normalTab);
  await triggerClickOn(splitTab1, { ctrlKey: true });

  is(gBrowser.selectedTab, normalTab, "normalTab is active");
  is(gBrowser.selectedTabs.length, 2, "Two selected tabs");
  ok(splitTab1.multiselected, "Splitview tab is multiselected");

  await withTabMenu(normalTab, async (_m1, _m2, ungroupTabItem) => {
    Assert.ok(!ungroupTabItem.hidden, "ungroupTabItem is visible");
    ungroupTabItem.click();
  });

  Assert.ok(splitTab1.splitview, "splitTab1 is still in a split view");
  Assert.ok(splitTab2.splitview, "splitTab2 is still in a split view");
  Assert.equal(
    splitTab1.splitview,
    splitTab2.splitview,
    "Both split tabs remain in the same split view"
  );
  Assert.ok(!splitTab1.group, "splitTab1 is no longer in the group");
  Assert.ok(!splitTab2.group, "splitTab2 is no longer in the group");
  Assert.ok(!normalTab.group, "normalTab is no longer in the group");

  splitview.close();
  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
  }
});

/*
 * Tests that using "Add split view to group" on a split view containing an
 * about:newtab tab correctly adds all split view tabs to the saved group, and
 * that restoring the group afterwards succeeds. Regression test for bug 2019900.
 */
add_task(
  async function test_tabGroupSaveGroupRestoreAfterAddSplitViewWithNewTab() {
    let savedTab = BrowserTestUtils.addTab(gBrowser, "https://example.com");
    let savedGroup = gBrowser.addTabGroup([savedTab], {
      label: "Test group",
    });
    let savedGroupId = savedGroup.id;

    await BrowserTestUtils.browserLoaded(savedTab.linkedBrowser);
    await lazy.TabGroupTestUtils.saveAndCloseTabGroup(savedGroup);

    let sessionStoreGroups = SessionStore.getSavedTabGroups();
    Assert.equal(sessionStoreGroups.length, 1, "The group was saved");

    // Add a split view (one real tab + one about:newtab) to the saved group via
    // the context menu, exercising the "Add split view to group" UI path.
    let splitRealTab = BrowserTestUtils.addTab(gBrowser, "https://example.com");
    let splitNewTab = BrowserTestUtils.addTab(gBrowser, "about:newtab");
    await BrowserTestUtils.browserLoaded(splitRealTab.linkedBrowser);
    gBrowser.addTabSplitView([splitRealTab, splitNewTab]);

    // Pre-flush before opening the context menu (see bug 1973996).
    await lazy.TabStateFlusher.flush(splitRealTab.linkedBrowser);
    await lazy.TabStateFlusher.flush(splitNewTab.linkedBrowser);

    await withTabMenu(splitRealTab, async (_, moveTabToGroupItem) => {
      const savedGroupsMenu = moveTabToGroupItem.querySelector(
        "#context_moveTabToSavedGroup"
      );
      const savedGroupMenuItem = savedGroupsMenu.querySelector(
        `[tab-group-id="${savedGroupId}"]`
      );
      let tabClosePromises = [splitRealTab, splitNewTab].map(tab =>
        BrowserTestUtils.waitForEvent(tab, "TabClose")
      );
      savedGroupMenuItem.click();
      await Promise.all(tabClosePromises);
    });

    // Add another regular tab to the saved group
    let extraTab = BrowserTestUtils.addTab(gBrowser, "https://example.com");
    await BrowserTestUtils.browserLoaded(extraTab.linkedBrowser);
    await lazy.TabStateFlusher.flush(extraTab.linkedBrowser);

    let extraTabClosePromise = BrowserTestUtils.waitForEvent(
      extraTab,
      "TabClose"
    );
    SessionStore.addTabsToSavedGroup(savedGroupId, [extraTab]);
    gBrowser.removeTab(extraTab);
    await extraTabClosePromise;

    sessionStoreGroups = SessionStore.getSavedTabGroups();
    Assert.equal(sessionStoreGroups.length, 1, "Only one group exists");
    // The about:newtab from the split view is saveable when it has a splitViewId,
    // so the split view should be stored.
    Assert.equal(
      sessionStoreGroups[0].splitViews.length,
      1,
      "Split view data stored for the group"
    );
    // The original tab + the 2 split view tabs + the extra tab should be saved.
    Assert.equal(
      sessionStoreGroups[0].tabs.length,
      4,
      "Four tabs saved in the group"
    );

    // Restore the group; if the fix is absent the restoreTab error aborts the
    // restore loop and SSWindowStateReady never fires (test times out).
    let restorePromise = BrowserTestUtils.waitForEvent(
      window,
      "SSWindowStateReady"
    );
    SessionStore.openSavedTabGroup(savedGroupId, window);
    await restorePromise;

    Assert.equal(
      gBrowser.tabGroups.length,
      1,
      "One tab group exists on the tab strip"
    );
    let restoredGroup = gBrowser.tabGroups[0];
    Assert.equal(
      restoredGroup.id,
      savedGroupId,
      "The restored group has the correct id"
    );
    Assert.equal(restoredGroup.tabs.length, 4, "Four tabs were restored");

    await lazy.TabGroupTestUtils.removeTabGroup(restoredGroup);
    lazy.TabGroupTestUtils.forgetSavedTabGroups();
  }
);

// Context menu tests: "new tab to right" option
// ---

/**
 * Tests that the "new tab to right" context menu option will create the new
 * tab inside of the same tab group as the context menu tab when the insertion
 * point is between two tabs within the same tab group
 */
add_task(async function test_newTabToRightInsideGroup() {
  let [tab1, tab2, tab3] = createManyTabs(3);
  let group = gBrowser.addTabGroup([tab1, tab2, tab3]);

  await withNewTabFromTabMenu(tab2, newTab => {
    Assert.equal(newTab.group, group, "new tab should be in the tab group");
  });

  await removeTabGroup(group);
});

/**
 * Tests that the "new tab to right" context menu option will create the new
 * tab inside of the same tab group as the context menu tab when the context
 * menu tab is the last tab in the tab group
 */
add_task(async function test_newTabToRightAtEndOfGroup() {
  let [tab1, tab2, tab3] = createManyTabs(3);
  let group = gBrowser.addTabGroup([tab1, tab2, tab3]);

  await withNewTabFromTabMenu(tab3, newTab => {
    Assert.equal(newTab.group, group, "new tab should be in the tab group");
  });

  await removeTabGroup(group);
});

/**
 * Tests that the "new tab to right" context menu option will create the new
 * tab outside of any tab group when then context menu tab is to the left of
 * a tab that is inside of a tab group
 */
add_task(async function test_newTabToRightBeforeGroup() {
  let [tab1, tab2, tab3] = createManyTabs(3);
  let group = gBrowser.addTabGroup([tab2, tab3], { insertBefore: tab2 });

  await withNewTabFromTabMenu(tab1, async newTab => {
    Assert.ok(!newTab.group, "new tab should not be in a tab group");
  });

  await removeTabGroup(group);
  await BrowserTestUtils.removeTab(tab1);
});
