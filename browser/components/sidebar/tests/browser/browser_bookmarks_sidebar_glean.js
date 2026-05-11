/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);

const UPDATED_BOOKMARKS_PREF = "sidebar.updatedBookmarks.enabled";

const BOOKMARK_DIALOG_URL =
  "chrome://browser/content/places/bookmarkProperties.xhtml";

let testFolder;
let testBookmarks;

Services.fog.testResetFOG();

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      [UPDATED_BOOKMARKS_PREF, true],
      ["privacy.userContext.enabled", true],
    ],
  });
  await PlacesUtils.bookmarks.eraseEverything();

  testFolder = await PlacesUtils.bookmarks.insert({
    title: "Glean Test Folder",
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
  });
  testBookmarks = await PlacesUtils.bookmarks.insertTree({
    guid: testFolder.guid,
    children: [
      { title: "Mozilla", url: "https://www.mozilla.org/" },
      { title: "Example", url: "https://example.com/" },
    ],
  });
  await SidebarTestUtils.waitForInitialized(window);
});

registerCleanupFunction(async () => {
  await PlacesUtils.bookmarks.eraseEverything();
  SidebarTestUtils.closePanel(window);
  await SpecialPowers.popPrefEnv();
});

/**
 * Show the Bookmarks panel in the revamped sidebar and return the element +
 * its content window.
 *
 * @returns {Promise<{component: SidebarBookmarks, contentWindow: Window}>}
 *   The `<sidebar-bookmarks>` element and the sidebar's content window.
 */
async function showBookmarksSidebar() {
  await SidebarTestUtils.showPanel(window, "viewBookmarksSidebar");
  const { contentDocument, contentWindow } = SidebarController.browser;

  let component;
  await BrowserTestUtils.waitForMutationCondition(
    contentDocument.documentElement,
    { childList: true, subtree: true },
    () => (component = contentDocument.querySelector("sidebar-bookmarks"))
  );
  await component.updateComplete;

  await TestUtils.waitForCondition(
    () => bookmarkTreeContainsGuid(component.bookmarks, testFolder.guid),
    "component.bookmarks contains the test folder"
  );
  await SimpleTest.promiseFocus();
  return { component, contentWindow };
}

/**
 * Returns true if the bookmark tree contains a node with the given guid.
 *
 * @param {object} node
 *   A node from `SidebarBookmarks#getBookmarksList()`.
 * @param {string} guid
 * @returns {boolean}
 */
function bookmarkTreeContainsGuid(node, guid) {
  if (node.guid === guid) {
    return true;
  }
  const children = node.children ?? [];
  for (const child of children) {
    if (bookmarkTreeContainsGuid(child, guid)) {
      return true;
    }
  }
  return false;
}

/**
 * Assert that the given `browser.ui.interaction.sidebar_bookmarks` labeled
 * counter has the expected value.
 *
 * @param {string} label
 *   One of the labels declared in metrics.yaml.
 * @param {number} value
 *   The expected counter value.
 */
function assertLabeledCounterValue(label, value) {
  Assert.equal(
    Glean.browserUiInteraction.sidebarBookmarks[label].testGetValue(),
    value,
    `sidebar_bookmarks["${label}"] should be ${value}`
  );
}

/**
 * Navigate the sidebar tree to the "Glean Test Folder" created in setup,
 * expanding the Toolbar folder along the way.
 *
 * @param {SidebarBookmarks} component
 *   The `<sidebar-bookmarks>` element.
 * @returns {Promise<HTMLDetailsElement>}
 *   The `<details>` element representing the test folder.
 */
async function getTestFolderEl(component) {
  const toolbarList = component.bookmarkList;

  let toolbarFolder;
  await BrowserTestUtils.waitForMutationCondition(
    toolbarList.shadowRoot,
    { childList: true, subtree: true },
    () => (toolbarFolder = toolbarList.folderEls[0])
  );
  if (!toolbarFolder.open) {
    toolbarFolder.querySelector("summary").click();
    await BrowserTestUtils.waitForMutationCondition(
      toolbarFolder,
      { attributes: true },
      () => toolbarFolder.open
    );
  }
  return TestUtils.waitForCondition(() => {
    const tabList = toolbarFolder.querySelector("sidebar-bookmark-list");
    return (
      tabList && [...tabList.folderEls].find(f => f.guid === testFolder.guid)
    );
  }, "Test folder appears in the Toolbar folder");
}

/**
 * Navigate the sidebar tree down to the first test bookmark row inside the
 * "Glean Test Folder", expanding folders along the way.
 *
 * @param {SidebarBookmarks} component
 *   The `<sidebar-bookmarks>` element.
 * @returns {Promise<Element>}
 *   The `<sidebar-bookmark-row>` for the first bookmark.
 */
async function getTestBookmarkRow(component) {
  const testFolderEl = await getTestFolderEl(component);
  if (!testFolderEl.open) {
    testFolderEl.querySelector("summary").click();
    await BrowserTestUtils.waitForMutationCondition(
      testFolderEl,
      { attributes: true },
      () => testFolderEl.open
    );
  }
  return TestUtils.waitForCondition(() => {
    const tabList = testFolderEl.querySelector("sidebar-bookmark-list");
    return (
      tabList && [...tabList.rowEls].find(r => r.guid === testBookmarks[0].guid)
    );
  }, "Test bookmark appears in the test folder");
}

/**
 * Type into a field in the bookmark properties subdialog and confirm it with
 * the Enter key.
 *
 * @param {Window} dialog
 *   The subdialog's content window.
 * @param {string} fieldId
 *   The id of the input element to fill.
 * @param {string} text
 *   The text to type into the field.
 */
async function fillFieldAndAccept(dialog, fieldId, text) {
  await dialog.document.mozSubdialogReady;
  const field = dialog.document.getElementById(fieldId);
  field.focus();
  field.select();
  EventUtils.sendString(text, dialog);
  EventUtils.synthesizeKey("VK_RETURN", {}, dialog);
}

add_task(async function test_open_commands() {
  const { component } = await showBookmarksSidebar();
  const row = await getTestBookmarkRow(component);

  const promiseNewTab = BrowserTestUtils.waitForNewTab(gBrowser);
  await activateContextMenuItem(
    row.mainEl,
    "sidebar-bookmarks-context-open-in-tab"
  );
  assertLabeledCounterValue("open_in_new_tab", 1);
  BrowserTestUtils.removeTab(await promiseNewTab);

  const promiseWin = BrowserTestUtils.waitForNewWindow();
  await activateContextMenuItem(
    row.mainEl,
    "sidebar-bookmarks-context-open-in-window"
  );
  assertLabeledCounterValue("open_in_new_window", 1);
  await BrowserTestUtils.closeWindow(await promiseWin);

  const promisePrivateWin = BrowserTestUtils.waitForNewWindow();
  await activateContextMenuItem(
    row.mainEl,
    "sidebar-bookmarks-context-open-in-private-window"
  );
  assertLabeledCounterValue("open_in_private_window", 1);
  await BrowserTestUtils.closeWindow(await promisePrivateWin);
});

add_task(async function test_edit_vs_rename_folder_cancelled() {
  const { sidebarBookmarks } = Glean.browserUiInteraction;
  const { component } = await showBookmarksSidebar();

  info("Open the edit bookmark dialog and cancel it.");
  const row = await getTestBookmarkRow(component);
  let dialogClosed = BrowserTestUtils.promiseAlertDialogOpen(
    "cancel",
    BOOKMARK_DIALOG_URL,
    { isSubDialog: true }
  );
  await activateContextMenuItem(
    row.mainEl,
    "sidebar-bookmarks-context-edit-bookmark"
  );
  await dialogClosed;
  await TestUtils.waitForCondition(
    () => sidebarBookmarks.edit_bookmark_cancelled.testGetValue() === 1,
    "edit_bookmark_cancelled recorded"
  );
  await SimpleTest.promiseFocus();

  info("Open the rename folder dialog and cancel it.");
  const testFolderEl = await getTestFolderEl(component);
  const folderHeader = testFolderEl.querySelector("summary");
  dialogClosed = BrowserTestUtils.promiseAlertDialogOpen(
    "cancel",
    BOOKMARK_DIALOG_URL,
    { isSubDialog: true }
  );
  await activateContextMenuItem(
    folderHeader,
    "sidebar-bookmarks-context-edit-bookmark"
  );
  await dialogClosed;
  await TestUtils.waitForCondition(
    () =>
      sidebarBookmarks.rename_bookmark_folder_cancelled.testGetValue() === 1,
    "rename_bookmark_folder_cancelled recorded"
  );
  await SimpleTest.promiseFocus();
});

add_task(async function test_edit_vs_rename_folder_confirmed() {
  const { sidebarBookmarks } = Glean.browserUiInteraction;
  const { component } = await showBookmarksSidebar();

  info("Open the edit bookmark dialog and confirm it.");
  let dialogClosed = BrowserTestUtils.promiseAlertDialogOpen(
    null,
    BOOKMARK_DIALOG_URL,
    {
      isSubDialog: true,
      callback: async win => {
        await win.document.mozSubdialogReady;
        EventUtils.synthesizeKey("VK_RETURN", {}, win);
      },
    }
  );
  const row = await getTestBookmarkRow(component);
  await activateContextMenuItem(
    row.mainEl,
    "sidebar-bookmarks-context-edit-bookmark"
  );
  await dialogClosed;
  await TestUtils.waitForCondition(
    () => sidebarBookmarks.edit_bookmark_confirmed.testGetValue() === 1,
    "edit_bookmark_confirmed recorded"
  );
  await SimpleTest.promiseFocus();

  info("Open the rename folder dialog and confirm it.");
  dialogClosed = BrowserTestUtils.promiseAlertDialogOpen(
    null,
    BOOKMARK_DIALOG_URL,
    {
      isSubDialog: true,
      callback: async win => {
        await win.document.mozSubdialogReady;
        EventUtils.synthesizeKey("VK_RETURN", {}, win);
      },
    }
  );
  const testFolderEl = await getTestFolderEl(component);
  const folderHeader = testFolderEl.querySelector("summary");
  await activateContextMenuItem(
    folderHeader,
    "sidebar-bookmarks-context-edit-bookmark"
  );
  await dialogClosed;
  await TestUtils.waitForCondition(
    () =>
      sidebarBookmarks.rename_bookmark_folder_confirmed.testGetValue() === 1,
    "rename_bookmark_folder_confirmed recorded"
  );
  await SimpleTest.promiseFocus();
});

add_task(async function test_open_all_bookmarks() {
  const { component } = await showBookmarksSidebar();
  const testFolderEl = await getTestFolderEl(component);
  const folderHeader = testFolderEl.querySelector("summary");

  const initialTabCount = gBrowser.tabs.length;
  const promiseNewTab = BrowserTestUtils.waitForNewTab(gBrowser);
  await activateContextMenuItem(
    folderHeader,
    "sidebar-bookmarks-context-open-all-bookmarks"
  );
  await promiseNewTab;

  await TestUtils.waitForCondition(
    () =>
      Glean.browserUiInteraction.sidebarBookmarks.open_all_bookmarks.testGetValue() ===
      1,
    "open_all_bookmarks recorded"
  );
  while (gBrowser.tabs.length > initialTabCount) {
    await BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
  }
});

add_task(async function test_container_tab() {
  const { component } = await showBookmarksSidebar();
  const row = await getTestBookmarkRow(component);
  const containerMenu = document.getElementById(
    "sidebar-bookmarks-context-open-in-container-tab"
  );
  const containerPopup = document.getElementById(
    "sidebar-bookmarks-context-container-tab-popup"
  );

  const promiseNewTab = BrowserTestUtils.waitForNewTab(gBrowser);
  await activateContextMenuItem(row.mainEl, null, async () => {
    const subpopupShown = BrowserTestUtils.waitForPopupEvent(
      containerPopup,
      "shown"
    );
    containerMenu.openMenu(true);
    await subpopupShown;
    containerPopup.activateItem(containerPopup.firstElementChild);
  });
  assertLabeledCounterValue("open_in_new_container_tab", 1);
  BrowserTestUtils.removeTab(await promiseNewTab);
});

add_task(async function test_copy_bookmark_url() {
  const { component } = await showBookmarksSidebar();
  const row = await getTestBookmarkRow(component);
  await activateContextMenuItem(
    row.mainEl,
    "sidebar-bookmarks-context-copy-link"
  );
  assertLabeledCounterValue("copy_bookmark_url", 1);
});

add_task(async function test_add_bookmark_and_folder_cancelled() {
  const { component } = await showBookmarksSidebar();
  const row = await getTestBookmarkRow(component);

  info("Start the Add Bookmark flow. Cancel the resulting dialog box.");
  let promiseDialog = BrowserTestUtils.promiseAlertDialogOpen(
    "cancel",
    BOOKMARK_DIALOG_URL,
    { isSubDialog: true }
  );
  await activateContextMenuItem(
    row.mainEl,
    "sidebar-bookmarks-context-add-bookmark"
  );
  await promiseDialog;
  await TestUtils.waitForCondition(
    () =>
      Glean.browserUiInteraction.sidebarBookmarks.add_bookmark_cancelled.testGetValue() ===
      1,
    "add_bookmark_cancelled recorded"
  );
  assertLabeledCounterValue("add_bookmark_cancelled", 1);
  await SimpleTest.promiseFocus();

  info("Start the Add Folder flow. Cancel the resulting dialog box.");
  promiseDialog = BrowserTestUtils.promiseAlertDialogOpen(
    "cancel",
    BOOKMARK_DIALOG_URL,
    { isSubDialog: true }
  );
  await activateContextMenuItem(
    row.mainEl,
    "sidebar-bookmarks-context-add-folder"
  );
  await promiseDialog;
  await TestUtils.waitForCondition(
    () =>
      Glean.browserUiInteraction.sidebarBookmarks.add_bookmark_folder_cancelled.testGetValue() ===
      1,
    "add_bookmark_folder_cancelled recorded"
  );
  assertLabeledCounterValue("add_bookmark_folder_cancelled", 1);
  await SimpleTest.promiseFocus();
});

add_task(async function test_add_bookmark_and_folder_confirmed() {
  const { component } = await showBookmarksSidebar();
  const row = await getTestBookmarkRow(component);

  info("Start the Add Bookmark flow. Confirm the resulting dialog box.");
  let promiseDialog = BrowserTestUtils.promiseAlertDialogOpen(
    null,
    BOOKMARK_DIALOG_URL,
    {
      isSubDialog: true,
      callback: dialog =>
        fillFieldAndAccept(
          dialog,
          "editBMPanel_locationField",
          "https://example.org/glean-confirmed/"
        ),
    }
  );
  await activateContextMenuItem(
    row.mainEl,
    "sidebar-bookmarks-context-add-bookmark"
  );
  await promiseDialog;
  await TestUtils.waitForCondition(
    () =>
      Glean.browserUiInteraction.sidebarBookmarks.add_bookmark_confirmed.testGetValue() ===
      1,
    "add_bookmark_confirmed recorded"
  );
  assertLabeledCounterValue("add_bookmark_confirmed", 1);
  await SimpleTest.promiseFocus();

  info("Start the Add Folder flow. Confirm the resulting dialog box.");
  promiseDialog = BrowserTestUtils.promiseAlertDialogOpen(
    null,
    BOOKMARK_DIALOG_URL,
    {
      isSubDialog: true,
      callback: async dialog => {
        await dialog.document.mozSubdialogReady;
        EventUtils.synthesizeKey("VK_RETURN", {}, dialog);
      },
    }
  );
  await activateContextMenuItem(
    row.mainEl,
    "sidebar-bookmarks-context-add-folder"
  );
  await promiseDialog;
  await TestUtils.waitForCondition(
    () =>
      Glean.browserUiInteraction.sidebarBookmarks.add_bookmark_folder_confirmed.testGetValue() ===
      1,
    "add_bookmark_folder_confirmed recorded"
  );
  assertLabeledCounterValue("add_bookmark_folder_confirmed", 1);
  await SimpleTest.promiseFocus();
});

add_task(async function test_sort_by_name() {
  const { component } = await showBookmarksSidebar();
  const testFolderEl = await getTestFolderEl(component);
  const folderHeader = testFolderEl.querySelector("summary");
  await activateContextMenuItem(
    folderHeader,
    "sidebar-bookmarks-context-sort-by-name"
  );
  assertLabeledCounterValue("sort_bookmarks_by_name", 1);
});

add_task(async function test_add_separator() {
  const { component } = await showBookmarksSidebar();
  const row = await getTestBookmarkRow(component);

  const promiseAdded = PlacesTestUtils.waitForNotification("bookmark-added");
  await activateContextMenuItem(
    row.mainEl,
    "sidebar-bookmarks-context-add-separator"
  );
  assertLabeledCounterValue("add_separator", 1);
  const events = await promiseAdded;
  for (const ev of events) {
    if (ev.itemType === PlacesUtils.bookmarks.TYPE_SEPARATOR) {
      await PlacesUtils.bookmarks.remove(ev.guid);
    }
  }
});

add_task(async function test_cut_bookmark() {
  // Create a dedicated bookmark so we don't disrupt other tests.
  const cutTarget = await PlacesUtils.bookmarks.insert({
    title: "Cut Me",
    url: "https://example.org/cut",
    parentGuid: testFolder.guid,
  });

  const { component } = await showBookmarksSidebar();
  const testFolderEl = await getTestFolderEl(component);
  if (!testFolderEl.open) {
    testFolderEl.querySelector("summary").click();
    await BrowserTestUtils.waitForMutationCondition(
      testFolderEl,
      { attributes: true },
      () => testFolderEl.open
    );
  }
  const row = await TestUtils.waitForCondition(() => {
    const tabList = testFolderEl.querySelector("sidebar-bookmark-list");
    return tabList && [...tabList.rowEls].find(r => r.guid === cutTarget.guid);
  }, "Cut target bookmark is present.");

  const promiseRemoved =
    PlacesTestUtils.waitForNotification("bookmark-removed");
  await activateContextMenuItem(row.mainEl, "sidebar-bookmarks-context-cut");
  assertLabeledCounterValue("cut_bookmark", 1);
  await promiseRemoved;
});

add_task(async function test_search() {
  const { component, contentWindow } = await showBookmarksSidebar();

  EventUtils.synthesizeMouseAtCenter(component.searchInput, {}, contentWindow);
  EventUtils.sendString("mozilla", contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.searchQuery === "mozilla"
  );
  Assert.greaterOrEqual(
    Glean.browserUiInteraction.sidebarBookmarks.search.testGetValue(),
    1,
    "Search counter was incremented"
  );
});
