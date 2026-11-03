/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* globals browser */

ChromeUtils.defineLazyGetter(this, "SidebarTestUtils", () => {
  const { SidebarTestUtils: utils } = ChromeUtils.importESModule(
    "resource://testing-common/SidebarTestUtils.sys.mjs"
  );
  utils.init(this);
  return utils;
});

// Verifies the alternate tab context menu layout
// (browser.tabs.contextmenu.altstructure.enabled): the tab "move" operations
// are relocated into the "Move Tab to" submenu, the AI entrypoint collapses to a
// single "Summarize Page" item, and WebExtension items land in the trailing
// extensions section.

const ALT_PREF = "browser.tabs.contextmenu.altstructure.enabled";

add_setup(async function () {
  // Setting a provider fires onChatProviderChange, which auto-opens the GenAI
  // chat sidebar. That leaves a chat browser around that leaks a window at shutdown.
  // Disable it first, in its own pushPrefEnv, so the guard is in effect before the
  // provider change below.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.openSidebarOnProviderChange", false]],
  });
  await SpecialPowers.pushPrefEnv({
    set: [
      [ALT_PREF, true],
      ["browser.tabs.groups.enabled", true],
      // Make the AI page feature available so the summarize item is shown.
      ["browser.ml.chat.page", true],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });
});

// The "move" operations (positions, Select All Tabs) are flattened into the
// #context_moveTabOptions ("Move Tab to") submenu rather than sitting at the
// top level as in the classic layout. Tab-group entries stay out of it.
add_task(async function test_move_operations_in_submenu() {
  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    await openTabContextMenu(gBrowser.selectedTab);

    Assert.equal(
      document.getElementById("context_moveTabOptions").parentNode.id,
      "tabContextMenu",
      "'Move Tab to' is a top-level submenu"
    );

    for (const id of [
      "context_moveToStart",
      "context_moveToEnd",
      "context_openTabInWindow",
      "context_selectAllTabs",
    ]) {
      Assert.equal(
        document.getElementById(id).parentNode.id,
        "moveTabOptionsMenu",
        `#${id} is inside the "Move Tab to" submenu`
      );
    }

    // Group actions live in the "Add to Group" submenu instead.
    for (const id of [
      "context_moveTabToGroupNewGroup",
      "context_moveTabToSavedGroup",
    ]) {
      Assert.equal(
        document.getElementById(id).parentNode.id,
        "context_moveTabToGroupPopupMenu",
        `#${id} stays in its own (hidden) submenu`
      );
    }
    Assert.ok(
      document.getElementById("context_moveTabToGroup").hidden,
      "the classic group submenu is hidden"
    );
    Assert.ok(
      document.getElementById("context_moveTabToGroupSeparator").hidden,
      "no group separator in 'Move Tab to'"
    );

    const positions = [
      "context_openTabInWindow",
      "context_moveTabPositionsSeparator",
      "context_moveToStart",
      "context_moveToEnd",
    ].map(id => document.getElementById(id));
    for (let i = 1; i < positions.length; i++) {
      Assert.ok(
        positions[i - 1].compareDocumentPosition(positions[i]) &
          Node.DOCUMENT_POSITION_FOLLOWING,
        `#${positions[i].id} follows #${positions[i - 1].id}`
      );
    }
    Assert.ok(
      !document.getElementById("context_moveTabPositionsSeparator").hidden,
      "positions separator is shown"
    );

    // Close Duplicates is flattened into the "Close Multiple" submenu.
    Assert.equal(
      document.getElementById("context_closeDuplicateTabs").parentNode.id,
      "closeTabOptions",
      "Close Duplicates is inside the Close Multiple submenu"
    );

    await closeTabContextMenu();
  });
});

// The AI entrypoint is a single flat "Summarize Page" item; the full
// #context_askChat submenu is hidden.
add_task(async function test_single_summarize_item() {
  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    await openTabContextMenu(gBrowser.selectedTab);

    const askChat = document.getElementById("context_askChat");
    const summarize = document.getElementById("context_askChatSummarize");

    // buildTabSummarizeItem populates the flat item asynchronously.
    await TestUtils.waitForCondition(
      () => !summarize.hidden,
      "flat Summarize Page item is shown"
    );

    Assert.ok(
      askChat.hidden,
      "the ask-chat submenu is hidden in the alt layout"
    );
    Assert.equal(
      summarize.parentNode.id,
      "tabContextMenu",
      "the summarize item is a top-level item"
    );
    Assert.equal(
      summarize.label,
      "Summarize with AI",
      "the summarize item uses the alt layout label"
    );

    await closeTabContextMenu();
  });
});

// "Add to Group" is a top-level submenu holding New Group, the open groups and
// the Closed Groups submenu.
add_task(async function test_add_to_group_menu() {
  const otherTab = await addTabTo(gBrowser, "https://example.com/");
  const group = gBrowser.addTabGroup([otherTab], { label: "Alt Group" });
  const initialGroupSize = group.tabs.length;

  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    const tab = gBrowser.selectedTab;
    await openTabContextMenu(tab);

    const addToGroup = document.getElementById("context_addTabToGroup");
    Assert.equal(
      addToGroup.parentNode.id,
      "tabContextMenu",
      "'Add to Group' is a top-level submenu"
    );
    Assert.ok(!addToGroup.hidden, "'Add to Group' is shown");

    for (const id of [
      "context_addTabToGroupNewGroup",
      "add-tab-to-group-separator",
      "context_addTabToSavedGroup",
    ]) {
      Assert.equal(
        document.getElementById(id).parentNode.id,
        "context_addTabToGroupPopupMenu",
        `#${id} is inside the "Add to Group" submenu`
      );
    }

    const popup = addToGroup.menupopup;
    const popupShown = BrowserTestUtils.waitForPopupEvent(popup, "shown");
    addToGroup.openMenu(true);
    await popupShown;

    const groupItem = popup.querySelector(
      `:scope > [tab-group-id="${group.id}"]`
    );
    Assert.ok(groupItem, "the open group is listed in 'Add to Group'");

    const menuHidden = BrowserTestUtils.waitForPopupEvent(
      document.getElementById("tabContextMenu"),
      "hidden"
    );
    popup.activateItem(groupItem);
    await menuHidden;

    Assert.equal(tab.group, group, "tab moved into proper group");
    Assert.equal(
      group.tabs.length,
      initialGroupSize + 1,
      "group contains an extra tab"
    );
  });

  await removeTabGroup(group);
});

// "Add to Group" counts the selected tabs, and a split view has its own
// wording. Its label is only updated while a group exists to add to.
add_task(async function test_add_to_group_label() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.splitView.enabled", true]],
  });
  const groupTab = await addTabTo(gBrowser, "https://example.com/");
  const group = gBrowser.addTabGroup([groupTab]);
  const tab1 = await addTabTo(gBrowser, "https://example.com/");
  const tab2 = await addTabTo(gBrowser, "https://example.com/");
  const addToGroup = document.getElementById("context_addTabToGroup");

  await BrowserTestUtils.switchTab(gBrowser, tab1);
  await openTabContextMenu(tab1);
  Assert.deepEqual(
    document.l10n.getAttributes(addToGroup),
    { id: "tab-context-add-tab-to-group", args: { tabCount: 1 } },
    "one tab uses the singular wording"
  );
  await closeTabContextMenu();

  await triggerClickOn(tab2, { ctrlKey: true });
  await openTabContextMenu(tab1);
  Assert.deepEqual(
    document.l10n.getAttributes(addToGroup),
    { id: "tab-context-add-tab-to-group", args: { tabCount: 2 } },
    "two selected tabs are counted"
  );
  await closeTabContextMenu();
  gBrowser.clearMultiSelectedTabs();

  const splitView = gBrowser.addTabSplitView([tab1, tab2]);
  await openTabContextMenu(tab1);
  Assert.deepEqual(
    document.l10n.getAttributes(addToGroup),
    { id: "tab-context-add-split-view-to-group", args: null },
    "a split view uses its own string, without stale args"
  );
  await document.l10n.translateElements([addToGroup]);
  Assert.equal(
    addToGroup.label,
    "Add Tabs to Group",
    "a split view uses a non-specific plural string"
  );
  await closeTabContextMenu();

  splitView.close();
  await removeTabGroup(group);
  await SpecialPowers.popPrefEnv();
});

// With no groups to add to, "Add to Group" still counts the selected tabs, and
// it is hidden when tab groups are disabled.
add_task(async function test_add_to_group_without_groups() {
  Assert.equal(gBrowser.getAllTabGroups().length, 0, "no open groups");
  const tab1 = await addTabTo(gBrowser, "https://example.com/");
  const tab2 = await addTabTo(gBrowser, "https://example.com/");
  const addToGroup = document.getElementById("context_addTabToGroup");

  await BrowserTestUtils.switchTab(gBrowser, tab1);
  await triggerClickOn(tab2, { ctrlKey: true });
  await openTabContextMenu(tab1);
  Assert.ok(!addToGroup.hidden, "'Add to Group' is shown");
  Assert.deepEqual(
    document.l10n.getAttributes(addToGroup),
    { id: "tab-context-add-tab-to-group", args: { tabCount: 2 } },
    "two selected tabs are counted without any groups"
  );
  await closeTabContextMenu();
  gBrowser.clearMultiSelectedTabs();

  await openTabContextMenu(tab1);
  Assert.deepEqual(
    document.l10n.getAttributes(addToGroup).args,
    { tabCount: 1 },
    "the count is refreshed for a single tab"
  );
  await closeTabContextMenu();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.groups.enabled", false]],
  });
  await openTabContextMenu(tab1);
  Assert.ok(addToGroup.hidden, "'Add to Group' is hidden without tab groups");
  await closeTabContextMenu();
  await SpecialPowers.popPrefEnv();

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

// Items with an alternate Fluent id use it while the alt layout is active.
add_task(async function test_alt_l10n_ids() {
  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    await openTabContextMenu(gBrowser.selectedTab);

    const vertical = gBrowser.tabContainer.verticalMode ? "-vertical" : "";
    const expected = {
      context_moveToStart: "move-to-start-alt",
      context_moveToEnd: "move-to-end-alt",
      context_openTabInWindow: "move-to-new-window-alt",
      context_reopenInContainer: "tab-context-open-in-container",
      context_closeDuplicateTabs: "tab-context-close-duplicate-tabs-alt",
      context_closeTabsToTheStart: `close-tabs-to-the-start${vertical}-alt`,
      context_closeTabsToTheEnd: `close-tabs-to-the-end${vertical}-alt`,
      context_closeOtherTabs: "close-other-tabs-alt",
      context_moveTabOptions: "tab-context-move-tabs3",
    };
    for (const [id, l10nId] of Object.entries(expected)) {
      Assert.equal(
        document.l10n.getAttributes(document.getElementById(id)).id,
        l10nId,
        `#${id} uses ${l10nId}`
      );
    }

    Assert.ok(
      !document.getElementById("context_closeOtherTabsSeparator").hidden,
      "the separator before 'All Others' is shown"
    );

    await closeTabContextMenu();
  });
});

// With no closed tabs, "Reopen Closed Tab" is disabled and uses the singular
// wording rather than a count of zero.
add_task(async function test_undo_close_tab_singular_when_empty() {
  Services.obs.notifyObservers(null, "browser:purge-session-history");
  Assert.equal(
    SessionStore.getClosedTabCountForWindow(window),
    0,
    "no closed tabs remain"
  );

  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    await openTabContextMenu(gBrowser.selectedTab);

    const undoCloseTab = document.getElementById("context_undoCloseTab");
    Assert.ok(undoCloseTab.disabled, "'Reopen Closed Tab' is disabled");
    Assert.equal(
      document.l10n.getAttributes(undoCloseTab).args.tabCount,
      1,
      "'Reopen Closed Tab' uses the singular wording"
    );

    await closeTabContextMenu();
  });
});

// A WebExtension tab context menu item appears in the trailing extensions
// section (after the layout's managed items), which the alt layout leaves open.
add_task(async function test_extension_item_in_extensions_section() {
  const extension = ExtensionTestUtils.loadExtension({
    manifest: { permissions: ["menus"] },
    background() {
      browser.menus.create(
        { id: "alt-ext-item", title: "Alt Ext Item", contexts: ["tab"] },
        () => browser.test.sendMessage("ready")
      );
    },
  });
  await extension.startup();
  await extension.awaitMessage("ready");

  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    const menu = await openTabContextMenu(gBrowser.selectedTab);

    let extItem;
    await BrowserTestUtils.waitForMutationCondition(
      menu,
      { childList: true },
      () => {
        extItem = menu.querySelector(':scope > menuitem[label="Alt Ext Item"]');
        return !!extItem;
      }
    );

    Assert.equal(
      extItem.parentNode,
      menu,
      "extension item is a top-level child of the tab context menu"
    );
    Assert.ok(
      document
        .getElementById("context_undoCloseTab")
        .compareDocumentPosition(extItem) & Node.DOCUMENT_POSITION_FOLLOWING,
      "extension item follows the layout's managed items (trailing extensions section)"
    );

    await closeTabContextMenu();
  });

  await extension.unload();
  // Close the panel opened by installing the extension
  SidebarTestUtils.closePanel(window);
});
