/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);
const { SidebarTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SidebarTestUtils.sys.mjs"
);

const BOOKMARK_DIALOG_URL =
  "chrome://browser/content/places/bookmarkProperties.xhtml";

let folder;
let bookmarks;

Services.fog.testResetFOG();
SidebarTestUtils.init(this);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.revamp", false]],
  });
  folder = await PlacesUtils.bookmarks.insert({
    title: "Glean Test Folder",
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
  });
  bookmarks = await PlacesUtils.bookmarks.insertTree({
    guid: folder.guid,
    children: [
      { title: "Mozilla", url: "https://www.mozilla.org/" },
      { title: "Example", url: "https://example.com/" },
    ],
  });
  await SidebarTestUtils.waitForInitialized(window);
});

// Bug 2036783 - Investigate leaks for legacy bookmarks panel
// The places context menu icons have intersection observers that aren't torn
// down cleanly when the sidebar window unloads, leaking the window until
// shutdown. As a workaround, remove those images from the menu before hiding.
async function _withSidebarTree(type, taskFn, win) {
  return withSidebarTree(
    type,
    async tree => {
      try {
        await taskFn(tree);
      } finally {
        const contextMenu = tree.ownerDocument.getElementById("placesContext");
        for (const icon of contextMenu.getElementsByClassName("menu-icon")) {
          icon.remove();
        }
        PlacesUIUtils.lastContextMenuTriggerNode = null;
      }
    },
    win
  );
}

registerCleanupFunction(async () => {
  await PlacesUtils.bookmarks.eraseEverything();
});

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
 * Right-click the tree's current selection to open the places context menu,
 * then activate the given menu item. Resolves once the `command` has fired
 * and the popup has fully closed.
 *
 * @param {XULTreeElement} tree
 *   The sidebar tree element returned by `withSidebarTree`.
 * @param {string} menuItemId
 *   The id of the `<menuitem>` to activate.
 */
async function activateContextMenuItem(tree, menuItemId) {
  const doc = tree.ownerDocument;
  const placesContext = doc.getElementById("placesContext");

  const popupShown = BrowserTestUtils.waitForPopupEvent(placesContext, "shown");
  synthesizeClickOnSelectedTreeCell(tree, { type: "contextmenu" });
  await popupShown;

  const popupHidden = BrowserTestUtils.waitForPopupEvent(
    placesContext,
    "hidden"
  );
  placesContext.activateItem(doc.getElementById(menuItemId));
  return popupHidden;
}

add_task(async function test_search_label() {
  await _withSidebarTree("bookmarks", async tree => {
    let searchBox = tree.ownerDocument.getElementById("search-box");
    await setSearch(searchBox, "mozilla");
  });

  assertLabeledCounterValue("search", 1);
});

add_task(async function test_open_commands() {
  await _withSidebarTree("bookmarks", async tree => {
    tree.selectItems([bookmarks[0].guid]);

    const promiseNewTab = BrowserTestUtils.waitForNewTab(gBrowser);
    await activateContextMenuItem(tree, "placesContext_open:newtab");
    assertLabeledCounterValue("open_in_new_tab", 1);
    BrowserTestUtils.removeTab(await promiseNewTab);

    const promiseWin = BrowserTestUtils.waitForNewWindow();
    await activateContextMenuItem(tree, "placesContext_open:newwindow");
    assertLabeledCounterValue("open_in_new_window", 1);
    await BrowserTestUtils.closeWindow(await promiseWin);

    const promisePrivateWin = BrowserTestUtils.waitForNewWindow();
    await activateContextMenuItem(tree, "placesContext_open:newprivatewindow");
    assertLabeledCounterValue("open_in_private_window", 1);
    await BrowserTestUtils.closeWindow(await promisePrivateWin);
  });
});

add_task(async function test_edit_vs_rename_folder_cancelled() {
  const { sidebarBookmarks } = Glean.browserUiInteraction;
  await _withSidebarTree("bookmarks", async tree => {
    tree.selectItems([bookmarks[0].guid]);
    let dialogClosed = BrowserTestUtils.promiseAlertDialogOpen(
      "cancel",
      BOOKMARK_DIALOG_URL,
      { isSubDialog: true }
    );
    await activateContextMenuItem(tree, "placesContext_show_bookmark:info");
    await dialogClosed;
    await TestUtils.waitForCondition(
      () => sidebarBookmarks.edit_bookmark_cancelled.testGetValue() === 1,
      "edit_bookmark_cancelled recorded"
    );
    await SimpleTest.promiseFocus();

    tree.selectItems([folder.guid]);
    dialogClosed = BrowserTestUtils.promiseAlertDialogOpen(
      "cancel",
      BOOKMARK_DIALOG_URL,
      { isSubDialog: true }
    );
    await activateContextMenuItem(tree, "placesContext_show_folder:info");
    await dialogClosed;
    await TestUtils.waitForCondition(
      () =>
        sidebarBookmarks.rename_bookmark_folder_cancelled.testGetValue() === 1,
      "rename_bookmark_folder_cancelled recorded"
    );
    await SimpleTest.promiseFocus();
  });
});

add_task(async function test_edit_vs_rename_folder_confirmed() {
  const { sidebarBookmarks } = Glean.browserUiInteraction;
  await _withSidebarTree("bookmarks", async tree => {
    tree.selectItems([bookmarks[0].guid]);
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
    await activateContextMenuItem(tree, "placesContext_show_bookmark:info");
    await dialogClosed;
    await TestUtils.waitForCondition(
      () => sidebarBookmarks.edit_bookmark_confirmed.testGetValue() === 1,
      "edit_bookmark_confirmed recorded"
    );
    await SimpleTest.promiseFocus();

    tree.selectItems([folder.guid]);
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
    await activateContextMenuItem(tree, "placesContext_show_folder:info");
    await dialogClosed;
    await TestUtils.waitForCondition(
      () =>
        sidebarBookmarks.rename_bookmark_folder_confirmed.testGetValue() === 1,
      "rename_bookmark_folder_confirmed recorded"
    );
    await SimpleTest.promiseFocus();
  });
});

add_task(async function test_copy_bookmark_url() {
  await _withSidebarTree("bookmarks", async tree => {
    tree.selectItems([bookmarks[0].guid]);
    await activateContextMenuItem(tree, "placesContext_copy");
    assertLabeledCounterValue("copy_bookmark_url", 1);
  });
});

add_task(async function test_cut_bookmark() {
  await _withSidebarTree("bookmarks", async tree => {
    tree.selectItems([bookmarks[0].guid]);
    await activateContextMenuItem(tree, "placesContext_cut");
    assertLabeledCounterValue("cut_bookmark", 1);
  });
});

add_task(async function test_add_bookmark_and_folder_cancelled() {
  const { sidebarBookmarks } = Glean.browserUiInteraction;
  await _withSidebarTree("bookmarks", async tree => {
    tree.selectItems([bookmarks[0].guid]);

    info("Start the Add Bookmark flow. Cancel the resulting dialog box.");
    let dialogClosed = BrowserTestUtils.promiseAlertDialogOpen(
      "cancel",
      BOOKMARK_DIALOG_URL,
      { isSubDialog: true }
    );
    await activateContextMenuItem(tree, "placesContext_new:bookmark");
    await dialogClosed;
    await TestUtils.waitForCondition(
      () => sidebarBookmarks.add_bookmark_cancelled.testGetValue() === 1,
      "add_bookmark_cancelled recorded"
    );
    await SimpleTest.promiseFocus();

    info("Start the Add Folder flow. Cancel the resulting dialog box.");
    dialogClosed = BrowserTestUtils.promiseAlertDialogOpen(
      "cancel",
      BOOKMARK_DIALOG_URL,
      { isSubDialog: true }
    );
    await activateContextMenuItem(tree, "placesContext_new:folder");
    await dialogClosed;
    await TestUtils.waitForCondition(
      () => sidebarBookmarks.add_bookmark_folder_cancelled.testGetValue() === 1,
      "add_bookmark_folder_cancelled recorded"
    );
    await SimpleTest.promiseFocus();
  });
});

add_task(async function test_add_bookmark_and_folder_confirmed() {
  const { sidebarBookmarks } = Glean.browserUiInteraction;
  await _withSidebarTree("bookmarks", async tree => {
    tree.selectItems([bookmarks[0].guid]);

    info("Start the Add Bookmark flow. Confirm the resulting dialog box.");
    let dialogClosed = BrowserTestUtils.promiseAlertDialogOpen(
      null,
      BOOKMARK_DIALOG_URL,
      {
        isSubDialog: true,
        callback: async win => {
          await win.document.mozSubdialogReady;
          const field = win.document.getElementById(
            "editBMPanel_locationField"
          );
          field.focus();
          field.select();
          EventUtils.sendString("https://example.org/glean-confirmed/", win);
          EventUtils.synthesizeKey("VK_RETURN", {}, win);
        },
      }
    );
    await activateContextMenuItem(tree, "placesContext_new:bookmark");
    await dialogClosed;
    await TestUtils.waitForCondition(
      () => sidebarBookmarks.add_bookmark_confirmed.testGetValue() === 1,
      "add_bookmark_confirmed recorded"
    );
    await SimpleTest.promiseFocus();

    info("Start the Add Folder flow. Confirm the resulting dialog box.");
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
    await activateContextMenuItem(tree, "placesContext_new:folder");
    await dialogClosed;
    await TestUtils.waitForCondition(
      () => sidebarBookmarks.add_bookmark_folder_confirmed.testGetValue() === 1,
      "add_bookmark_folder_confirmed recorded"
    );
    await SimpleTest.promiseFocus();
  });
});

add_task(async function test_add_separator() {
  await _withSidebarTree("bookmarks", async tree => {
    tree.selectItems([bookmarks[0].guid]);

    const promiseAdded = PlacesTestUtils.waitForNotification("bookmark-added");
    await activateContextMenuItem(tree, "placesContext_new:separator");
    assertLabeledCounterValue("add_separator", 1);
    const events = await promiseAdded;
    for (const ev of events) {
      if (ev.itemType === PlacesUtils.bookmarks.TYPE_SEPARATOR) {
        await PlacesUtils.bookmarks.remove(ev.guid);
      }
    }
  });
});

add_task(async function test_sort_by_name() {
  await _withSidebarTree("bookmarks", async tree => {
    tree.selectItems([folder.guid]);
    await activateContextMenuItem(tree, "placesContext_sortBy:name");
    assertLabeledCounterValue("sort_bookmarks_by_name", 1);
  });
});

add_task(async function test_open_all_bookmarks_label() {
  await _withSidebarTree("bookmarks", async tree => {
    tree.selectItems([folder.guid]);

    const promiseNewTab = BrowserTestUtils.waitForNewTab(gBrowser);
    await activateContextMenuItem(
      tree,
      "placesContext_openBookmarkContainer:tabs"
    );
    await promiseNewTab;
    while (gBrowser.tabs.length > 1) {
      await BrowserTestUtils.removeTab(gBrowser.tabs[1]);
    }
  });

  assertLabeledCounterValue("open_all_bookmarks", 1);
});

add_task(async function test_open_in_container_tab_label() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.userContext.enabled", true]],
  });

  await _withSidebarTree("bookmarks", async tree => {
    let sidebarDoc = tree.ownerDocument;
    let placesContext = sidebarDoc.getElementById("placesContext");

    tree.selectItems([bookmarks[0].guid]);

    let popupShown = BrowserTestUtils.waitForPopupEvent(placesContext, "shown");
    synthesizeClickOnSelectedTreeCell(tree, { type: "contextmenu" });
    await popupShown;

    let containerMenu = sidebarDoc.getElementById(
      "placesContext_open:newcontainertab"
    );
    let containerPopup = sidebarDoc.getElementById(
      "placesContext_open_newcontainertab_popup"
    );
    let subpopupShown = BrowserTestUtils.waitForPopupEvent(
      containerPopup,
      "shown"
    );
    containerMenu.openMenu(true);
    await subpopupShown;

    let promiseNewTab = BrowserTestUtils.waitForNewTab(gBrowser);
    containerPopup.firstElementChild.click();
    BrowserTestUtils.removeTab(await promiseNewTab);
  });

  assertLabeledCounterValue("open_in_new_container_tab", 1);
  await SpecialPowers.popPrefEnv();
});
