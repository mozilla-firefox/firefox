/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);

const { showBookmarksSidebar, openToolbarFolder } = SidebarTestUtils.bookmarks;

const TEST_URL = "https://example.com/";
const UPDATED_BOOKMARKS_PREF = "sidebar.updatedBookmarks.enabled";
const BOOKMARK_DIALOG_URL =
  "chrome://browser/content/places/bookmarkProperties.xhtml";

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [[UPDATED_BOOKMARKS_PREF, true]],
  });
});

async function addFolderViaContextMenu(triggerEl) {
  const dialogPromise = BrowserTestUtils.promiseAlertDialogOpen(
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
    triggerEl,
    "sidebar-bookmarks-context-add-folder"
  );
  await dialogPromise;
  // Wait for the dialog's save() to fully complete. NewFolder inserts the
  // folder at the end of the parent and then moves it to the requested index
  // in a second step (bug 1388097); the bookmark-added notification fires
  // before that move, so we must wait for the whole operation to settle.
  return PlacesUIUtils.lastBookmarkDialogDeferred.promise;
}

async function addBookmarkViaContextMenu(triggerEl, url) {
  const dialogPromise = BrowserTestUtils.promiseAlertDialogOpen(
    null,
    BOOKMARK_DIALOG_URL,
    {
      isSubDialog: true,
      callback: async dialog => {
        await dialog.document.mozSubdialogReady;
        // The add-bookmark dialog opens with an empty location, which keeps
        // the accept button disabled. Set a URL and fire an input event so the
        // button enables before we confirm.
        const locationField = dialog.document.getElementById(
          "editBMPanel_locationField"
        );
        locationField.value = url;
        locationField.dispatchEvent(
          new dialog.Event("input", { bubbles: true })
        );
        EventUtils.synthesizeKey("VK_RETURN", {}, dialog);
      },
    }
  );
  await activateContextMenuItem(
    triggerEl,
    "sidebar-bookmarks-context-add-bookmark"
  );
  await dialogPromise;
  return PlacesUIUtils.lastBookmarkDialogDeferred.promise;
}

add_setup(async function () {
  await PlacesUtils.bookmarks.eraseEverything();
  await SidebarTestUtils.waitForInitialized(window);
  registerCleanupFunction(async () => {
    await PlacesUtils.bookmarks.eraseEverything();
    SidebarTestUtils.closePanel(window);
  });
});

add_task(async function test_bookmarks_panel_opens() {
  const { component } = await showBookmarksSidebar(window);
  ok(component, "Bookmarks panel component is present.");

  ok(SidebarController.panelHeader, "Panel header is rendered.");
  ok(component.searchInput, "Search input is rendered.");

  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_shows_toolbar_folder() {
  const bookmark = await addBookmark();

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;
  await BrowserTestUtils.waitForMutationCondition(
    tabList.shadowRoot,
    { childList: true, subtree: true },
    () => tabList.folderEls[0]
  );

  const folders = tabList.folderEls;
  Assert.greater(folders.length, 0, "At least one folder is rendered.");

  const summaries = [...folders].map(d =>
    d.querySelector("summary").textContent.trim()
  );
  ok(
    summaries.some(s => !!s.length),
    "Folder summaries have content."
  );

  await PlacesUtils.bookmarks.remove(bookmark);
  SidebarTestUtils.closePanel(window);
});

async function testBookmarkTitleRendering(bookmarkInfo, title, message) {
  const bookmark = await addBookmark(bookmarkInfo);

  const { component } = await showBookmarksSidebar(window);
  const row = await getBookmarkRow(component.bookmarkList, bookmark);
  Assert.equal(row.title, title, message);
  await assertRowTooltip(
    row,
    `${title}\n${bookmark.url.href}`,
    "Tooltip shows the title and URL."
  );

  await PlacesUtils.bookmarks.remove(bookmark);
  SidebarTestUtils.closePanel(window);
}

add_task(async function test_bookmarks_shows_bookmark_in_folder() {
  await testBookmarkTitleRendering(
    { title: "My Test Page", url: "https://example.com" },
    "My Test Page",
    "If a bookmark has title set, use that title."
  );
  await testBookmarkTitleRendering(
    { title: "", url: "https://example.com" },
    "example.com/",
    "If a bookmark has no title, generate one from its URI."
  );
  await testBookmarkTitleRendering(
    { title: "", url: "about:robots" },
    PlacesUIUtils.promptLocalization.formatValueSync("places-no-title"),
    "If the URI is an about: page, use placeholder string."
  );
});

add_task(async function test_bookmarks_search_filters_results() {
  const bm1 = await addBookmark({
    title: "Apple Cider",
    url: "https://example.com/",
  });
  const bm2 = await addBookmark({
    title: "Banana Split",
    url: "https://example.org/",
  });

  const { component, contentWindow } = await showBookmarksSidebar(window);
  ok(component.searchInput, "Search input is present.");

  info("Search for 'Apple'.");
  EventUtils.synthesizeMouseAtCenter(component.searchInput, {}, contentWindow);
  EventUtils.sendString("Apple", contentWindow);

  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.searchResults?.length > 0
  );

  Assert.equal(component.searchQuery, "Apple", "Search query is set.");
  const results = component.searchResults;
  Assert.equal(results.length, 1, "One search result found.");
  Assert.equal(results[0].title, "Apple Cider", "Correct bookmark found.");

  info("Search for a term with no matches.");
  EventUtils.synthesizeMouseAtCenter(component.searchInput, {}, contentWindow);
  EventUtils.sendString(" ZZZNOMATCH", contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.searchResults?.length === 0 && component.searchQuery !== ""
  );
  Assert.equal(
    component.searchResults.length,
    0,
    "No results for bogus query."
  );

  await PlacesUtils.bookmarks.remove(bm1);
  await PlacesUtils.bookmarks.remove(bm2);
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_search_results_show_tab_list() {
  const bm = await addBookmark({ title: "SearchTarget", url: TEST_URL });

  const { component, contentWindow } = await showBookmarksSidebar(window);

  EventUtils.synthesizeMouseAtCenter(component.searchInput, {}, contentWindow);
  EventUtils.sendString("SearchTarget", contentWindow);

  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.bookmarkList && component.searchResults?.length === 1
  );

  ok(component.bookmarkList, "Search results tab list is shown.");

  const header = component.shadowRoot.querySelector(
    "[data-l10n-id='firefoxview-search-results-header']"
  );
  ok(header, "Search results header is shown.");

  await PlacesUtils.bookmarks.remove(bm);
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_searchbox_focus_and_context_menu() {
  const { component, contentWindow } = await showBookmarksSidebar(window);
  const { searchInput } = component;

  ok(component.shadowRoot.activeElement, "check activeElement is present");
  Assert.equal(
    component.shadowRoot.activeElement,
    searchInput,
    "Check search box is focused"
  );

  const promisePopupShown = BrowserTestUtils.waitForEvent(
    contentWindow,
    "popupshown"
  );
  EventUtils.synthesizeMouseAtCenter(
    searchInput,
    { type: "contextmenu", button: 2 },
    contentWindow
  );
  const { target: menu } = await promisePopupShown;
  Assert.equal(
    menu.id,
    "textbox-contextmenu",
    "The edit context menu is shown for the search input."
  );
  menu.hidePopup();

  SidebarController.hide();
});

add_task(async function test_bookmarks_folder_expand_collapse() {
  const folder = await addFolder("ExpandableFolder");
  await addBookmark({ title: "Inside Folder", parentGuid: folder.guid });

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;

  const toolbarDetails = await openToolbarFolder(tabList);
  ok(toolbarDetails, "Toolbar folder details element found.");

  const nestedList = toolbarDetails.querySelector("sidebar-bookmark-list");
  await BrowserTestUtils.waitForMutationCondition(
    nestedList.shadowRoot,
    { childList: true, subtree: true },
    () => nestedList.folderEls[0]
  );

  const folderDetails = nestedList.folderEls[0];
  ok(folderDetails, "Nested folder is rendered as a <details> element.");

  const summary = folderDetails.querySelector("summary");
  Assert.equal(
    summary.textContent.trim(),
    "ExpandableFolder",
    "Folder label matches."
  );

  const wasOpen = folderDetails.open;
  summary.click();
  await BrowserTestUtils.waitForMutationCondition(
    folderDetails,
    { attributes: true },
    () => folderDetails.open !== wasOpen
  );
  Assert.notEqual(folderDetails.open, wasOpen, "Folder toggled open/closed.");

  await PlacesUtils.bookmarks.remove({ guid: folder.guid });
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_panel_updates_on_places_event() {
  const { component } = await showBookmarksSidebar(window);

  const bm = await addBookmark({ title: "Dynamic Bookmark" });

  function findInTree(node, title) {
    if (node.title === title) {
      return true;
    }
    for (const child of node.children ?? []) {
      if (findInTree(child, title)) {
        return true;
      }
    }
    return false;
  }

  await BrowserTestUtils.waitForMutationCondition(
    component.bookmarkList.shadowRoot,
    { childList: true, subtree: true },
    () => findInTree(component.bookmarks, "Dynamic Bookmark")
  );

  await PlacesUtils.bookmarks.remove(bm);

  await BrowserTestUtils.waitForMutationCondition(
    component.bookmarkList.shadowRoot,
    { childList: true, subtree: true },
    () => !findInTree(component.bookmarks, "Dynamic Bookmark")
  );

  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_context_menu_bookmark() {
  const bm = await addBookmark({ title: "Context Menu Bookmark" });

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;

  const toolbarDetails = await openToolbarFolder(tabList);

  const nestedList = toolbarDetails.querySelector("sidebar-bookmark-list");
  await BrowserTestUtils.waitForMutationCondition(
    nestedList.shadowRoot,
    { childList: true, subtree: true },
    () => nestedList.rowEls[0]
  );

  const row = nestedList.rowEls[0];
  const contextMenu = SidebarController.currentContextMenu;
  await openAndWaitForContextMenu(contextMenu, row.mainEl, () => {});

  ok(
    !document.getElementById("sidebar-bookmarks-context-open-in-tab").hidden,
    "Open in tab is visible for a bookmark."
  );
  ok(
    document.getElementById("sidebar-bookmarks-context-open-all-bookmarks")
      .hidden,
    "Open all bookmarks is hidden for a bookmark."
  );
  ok(
    !document.getElementById("sidebar-bookmarks-context-edit-bookmark").hidden,
    "Edit bookmark is visible."
  );
  ok(
    !document.getElementById("sidebar-bookmarks-context-delete-bookmark")
      .hidden,
    "Delete bookmark is visible."
  );
  ok(
    !document.getElementById("sidebar-bookmarks-context-copy-link").hidden,
    "Copy link is visible for a bookmark."
  );
  ok(
    document.getElementById("sidebar-bookmarks-context-show-in-folder").hidden,
    "Show in Folder is hidden when not searching."
  );

  contextMenu.hidePopup();
  await PlacesUtils.bookmarks.remove(bm);
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_search_context_menu_show_in_folder() {
  const folder = await addFolder("Outer");
  const bm = await addBookmark({
    title: "FindMe",
    url: TEST_URL,
    parentGuid: folder.guid,
  });

  const { component, contentWindow } = await showBookmarksSidebar(window);

  info("Search for the nested bookmark.");
  EventUtils.synthesizeMouseAtCenter(component.searchInput, {}, contentWindow);
  EventUtils.sendString("FindMe", contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.searchResults?.length === 1
  );

  const resultsList = component.bookmarkList;
  await BrowserTestUtils.waitForMutationCondition(
    resultsList.shadowRoot,
    { childList: true, subtree: true },
    () => resultsList.rowEls[0]?.guid === bm.guid
  );
  const row = resultsList.rowEls[0];

  const contextMenu = SidebarController.currentContextMenu;
  await openAndWaitForContextMenu(contextMenu, row.mainEl, () => {});

  ok(
    !document.getElementById("sidebar-bookmarks-context-show-in-folder").hidden,
    "Show in Folder is visible in search results."
  );
  ok(
    document.getElementById("sidebar-bookmarks-context-copy-link").hidden,
    "Copy Link is hidden in search results."
  );
  ok(
    document.getElementById("sidebar-bookmarks-context-paste").hidden,
    "Paste is hidden in search results."
  );
  for (const id of [
    "sidebar-bookmarks-context-sep-edit-copy",
    "sidebar-bookmarks-context-sep-add",
    "sidebar-bookmarks-context-add-bookmark",
    "sidebar-bookmarks-context-add-folder",
    "sidebar-bookmarks-context-add-separator",
  ]) {
    ok(
      document.getElementById(id).hidden,
      `${id} is hidden in search results.`
    );
  }

  info("Activate Show in Folder.");
  const promiseHidden = BrowserTestUtils.waitForPopupEvent(
    contextMenu,
    "hidden"
  );
  contextMenu.activateItem(
    document.getElementById("sidebar-bookmarks-context-show-in-folder")
  );
  await promiseHidden;

  await TestUtils.waitForCondition(
    () => component.searchQuery === "",
    "Search is cleared after Show in Folder."
  );

  const tabList = component.bookmarkList;
  await BrowserTestUtils.waitForMutationCondition(
    tabList.shadowRoot,
    { childList: true, subtree: true },
    () => tabList.folderEls[0]
  );

  const findRow = list => {
    if (!list) {
      return null;
    }
    for (const r of list.rowEls ?? []) {
      if (r.guid === bm.guid) {
        return r;
      }
    }
    for (const details of list.folderEls ?? []) {
      const sublist = details.querySelector("sidebar-bookmark-list");
      const found = findRow(sublist);
      if (found) {
        return found;
      }
    }
    return null;
  };

  await TestUtils.waitForCondition(
    () => findRow(tabList),
    "The bookmark row appears in the tree view after Show in Folder."
  );
  const revealedRow = findRow(tabList);
  await TestUtils.waitForCondition(
    () => revealedRow.selected,
    "The bookmark row is visually selected after Show in Folder."
  );

  await PlacesUtils.bookmarks.remove(bm);
  await PlacesUtils.bookmarks.remove({ guid: folder.guid });
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_context_menu_folder() {
  const folder = await addFolder("Context Menu Folder");
  await addBookmark({ title: "In Folder", parentGuid: folder.guid });

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;

  const toolbarDetails = await openToolbarFolder(tabList);

  const nestedList = toolbarDetails.querySelector("sidebar-bookmark-list");
  await BrowserTestUtils.waitForMutationCondition(
    nestedList.shadowRoot,
    { childList: true, subtree: true },
    () => nestedList.folderEls[0]
  );

  const folderDetails = nestedList.folderEls[0];
  const summary = folderDetails.querySelector("summary");

  const contextMenu = SidebarController.currentContextMenu;
  await openAndWaitForContextMenu(contextMenu, summary, () => {});

  ok(
    !document.getElementById("sidebar-bookmarks-context-open-all-bookmarks")
      .hidden,
    "Open all bookmarks is visible for a folder."
  );
  ok(
    !document.getElementById("sidebar-bookmarks-context-open-all-bookmarks")
      .disabled,
    "Open all bookmarks is enabled for a folder with bookmark items."
  );
  ok(
    document.getElementById("sidebar-bookmarks-context-open-in-tab").hidden,
    "Open in tab is hidden for a folder."
  );
  ok(
    document.getElementById("sidebar-bookmarks-context-copy-link").hidden,
    "Copy link is hidden for a folder."
  );
  ok(
    !document.getElementById("sidebar-bookmarks-context-sort-by-name").hidden,
    "Sort by name is visible for a folder."
  );

  contextMenu.hidePopup();
  await PlacesUtils.bookmarks.remove({ guid: folder.guid });
  SidebarTestUtils.closePanel(window);
});

add_task(
  async function test_bookmarks_context_menu_folder_without_bookmark_items() {
    const folder = await addFolder("Subfolders Only");
    await addFolder("Nested Folder", folder.guid);

    const { component } = await showBookmarksSidebar(window);
    const tabList = component.bookmarkList;

    const toolbarDetails = await openToolbarFolder(tabList);

    const nestedList = toolbarDetails.querySelector("sidebar-bookmark-list");
    await BrowserTestUtils.waitForMutationCondition(
      nestedList.shadowRoot,
      { childList: true, subtree: true },
      () => nestedList.folderEls[0]
    );

    const folderDetails = nestedList.folderEls[0];
    const summary = folderDetails.querySelector("summary");

    const contextMenu = SidebarController.currentContextMenu;
    await openAndWaitForContextMenu(contextMenu, summary, () => {});

    ok(
      !document.getElementById("sidebar-bookmarks-context-open-all-bookmarks")
        .hidden,
      "Open all bookmarks is visible for a folder with only subfolders."
    );
    ok(
      document.getElementById("sidebar-bookmarks-context-open-all-bookmarks")
        .disabled,
      "Open all bookmarks is disabled for a folder with no bookmark items."
    );

    contextMenu.hidePopup();
    await PlacesUtils.bookmarks.remove({ guid: folder.guid });
    SidebarTestUtils.closePanel(window);
  }
);

add_task(async function test_add_folder_before_right_clicked_bookmark() {
  await addBookmark({ title: "Alpha", url: "https://example.com/a" });
  const bmB = await addBookmark({
    title: "Beta",
    url: "https://example.com/b",
  });
  await addBookmark({ title: "Gamma", url: "https://example.com/c" });

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;
  const rowB = await getBookmarkRow(tabList, bmB);
  const { index: indexB } = await PlacesUtils.bookmarks.fetch(bmB.guid);

  const newFolderGuid = await addFolderViaContextMenu(rowB.mainEl);
  info(`newFolderGuid: ${newFolderGuid}`);
  const newFolder = await PlacesUtils.bookmarks.fetch(newFolderGuid);
  info(`newFolder index: ${newFolder.index}`);

  is(
    newFolder.parentGuid,
    PlacesUtils.bookmarks.toolbarGuid,
    "New folder is created in the right-clicked bookmark's parent."
  );
  is(
    newFolder.index,
    indexB,
    "New folder is created just before the right-clicked bookmark."
  );
  const movedB = await PlacesUtils.bookmarks.fetch(bmB.guid);
  is(
    movedB.index,
    indexB + 1,
    "The right-clicked bookmark is pushed down by the new folder."
  );

  await PlacesUtils.bookmarks.eraseEverything();
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_add_folder_into_right_clicked_folder() {
  const folder = await addFolder("Parent Folder");
  await addBookmark({
    title: "Child One",
    url: "https://example.com/1",
    parentGuid: folder.guid,
  });
  await addBookmark({
    title: "Child Two",
    url: "https://example.com/2",
    parentGuid: folder.guid,
  });

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;
  const folderDetails = await getBookmarkFolder(tabList, folder);
  const summary = folderDetails.querySelector("summary");

  const newFolderGuid = await addFolderViaContextMenu(summary);

  const newFolder = await PlacesUtils.bookmarks.fetch(newFolderGuid);
  is(
    newFolder.parentGuid,
    folder.guid,
    "New folder is created inside the right-clicked folder."
  );
  is(
    newFolder.index,
    2,
    "New folder is appended at the end of the right-clicked folder."
  );

  await PlacesUtils.bookmarks.eraseEverything();
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_add_bookmark_before_right_clicked_bookmark() {
  await addBookmark({ title: "Alpha", url: "https://example.com/a" });
  const bmB = await addBookmark({
    title: "Beta",
    url: "https://example.com/b",
  });
  await addBookmark({ title: "Gamma", url: "https://example.com/c" });

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;
  const rowB = await getBookmarkRow(tabList, bmB);
  const { index: indexB } = await PlacesUtils.bookmarks.fetch(bmB.guid);

  const newBookmarkGuid = await addBookmarkViaContextMenu(
    rowB.mainEl,
    "https://example.com/new"
  );
  const newBookmark = await PlacesUtils.bookmarks.fetch(newBookmarkGuid);

  is(
    newBookmark.parentGuid,
    PlacesUtils.bookmarks.toolbarGuid,
    "New bookmark is created in the right-clicked bookmark's parent."
  );
  is(
    newBookmark.index,
    indexB,
    "New bookmark is created just before the right-clicked bookmark."
  );
  const movedB = await PlacesUtils.bookmarks.fetch(bmB.guid);
  is(
    movedB.index,
    indexB + 1,
    "The right-clicked bookmark is pushed down by the new bookmark."
  );

  await PlacesUtils.bookmarks.eraseEverything();
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_add_bookmark_into_right_clicked_folder() {
  const folder = await addFolder("Parent Folder");
  await addBookmark({
    title: "Child One",
    url: "https://example.com/1",
    parentGuid: folder.guid,
  });
  await addBookmark({
    title: "Child Two",
    url: "https://example.com/2",
    parentGuid: folder.guid,
  });

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;
  const folderDetails = await getBookmarkFolder(tabList, folder);
  const summary = folderDetails.querySelector("summary");

  const newBookmarkGuid = await addBookmarkViaContextMenu(
    summary,
    "https://example.com/new"
  );

  const newBookmark = await PlacesUtils.bookmarks.fetch(newBookmarkGuid);
  is(
    newBookmark.parentGuid,
    folder.guid,
    "New bookmark is created inside the right-clicked folder."
  );
  is(
    newBookmark.index,
    2,
    "New bookmark is appended at the end of the right-clicked folder."
  );

  await PlacesUtils.bookmarks.eraseEverything();
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_copy_writes_all_flavors() {
  const bookmark = await addBookmark({
    title: "Copy Me",
    url: "https://example.com/copy",
  });

  const { component } = await showBookmarksSidebar(window);
  const row = await getBookmarkRow(component.bookmarkList, bookmark);
  await activateContextMenuItem(row.mainEl, "sidebar-bookmarks-context-copy");

  for (const flavor of [
    PlacesUtils.TYPE_X_MOZ_PLACE,
    PlacesUtils.TYPE_X_MOZ_URL,
    PlacesUtils.TYPE_PLAINTEXT,
  ]) {
    ok(
      Services.clipboard.hasDataMatchingFlavors(
        [flavor],
        Ci.nsIClipboard.kGlobalClipboard
      ),
      `Clipboard has the ${flavor} flavor.`
    );
  }

  // Check text/plain flavor saved in clipboard holds the expected bookmark url
  const xferable = Cc["@mozilla.org/widget/transferable;1"].createInstance(
    Ci.nsITransferable
  );
  xferable.init(null);
  xferable.addDataFlavor(PlacesUtils.TYPE_PLAINTEXT);
  Services.clipboard.getData(xferable, Ci.nsIClipboard.kGlobalClipboard);
  const plain = {};
  xferable.getTransferData(PlacesUtils.TYPE_PLAINTEXT, plain);
  is(
    plain.value.QueryInterface(Ci.nsISupportsString).data,
    "https://example.com/copy",
    "text/plain flavor holds the bookmark URL."
  );

  Services.clipboard.emptyClipboard(Ci.nsIClipboard.kGlobalClipboard);
  await PlacesUtils.bookmarks.eraseEverything();
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_copy_paste_into_folder() {
  const bm = await addBookmark({
    title: "Copy and Paste Me",
    url: "https://example.com/paste",
  });
  const folder = await addFolder("Paste Target Folder");
  await addBookmark({
    title: "Inside Folder Already",
    url: "https://example.com/existing-in-paste-folder",
    parentGuid: folder.guid,
  });

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;
  const row = await getBookmarkRow(tabList, bm);
  await activateContextMenuItem(row.mainEl, "sidebar-bookmarks-context-copy");

  const folderDetails = await getBookmarkFolder(tabList, folder);
  const summary = folderDetails.querySelector("summary");
  const promiseAdded = PlacesTestUtils.waitForNotification("bookmark-added");

  await activateContextMenuItem(summary, "sidebar-bookmarks-context-paste");
  const [{ guid: pastedGuid }] = await promiseAdded;

  const pasted = await PlacesUtils.bookmarks.fetch(pastedGuid);
  is(
    pasted.parentGuid,
    folder.guid,
    "The copied bookmark is pasted inside the folder."
  );
  is(
    pasted.url.href,
    "https://example.com/paste",
    "The pasted bookmark has the right URL."
  );
  ok(
    await PlacesUtils.bookmarks.fetch(bm.guid),
    "The original bookmark still exists after a copy."
  );

  Services.clipboard.emptyClipboard(Ci.nsIClipboard.kGlobalClipboard);
  await PlacesUtils.bookmarks.eraseEverything();
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_cut_moves_after_paste() {
  const bm = await addBookmark({
    title: "Cut Me",
    url: "https://example.com/cut",
  });
  const folder = await addFolder("Paste Target Folder");
  await addBookmark({
    title: "Inside Folder Already",
    url: "https://example.com/existing",
    parentGuid: folder.guid,
  });

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;
  const row = await getBookmarkRow(tabList, bm);
  await activateContextMenuItem(row.mainEl, "sidebar-bookmarks-context-cut");

  ok(
    await PlacesUtils.bookmarks.fetch(bm.guid),
    "The bookmark still exists after being cut, before pasting."
  );

  const folderDetails = await getBookmarkFolder(tabList, folder);
  const summary = folderDetails.querySelector("summary");
  await activateContextMenuItem(summary, "sidebar-bookmarks-context-paste");

  await TestUtils.waitForCondition(async () => {
    const moved = await PlacesUtils.bookmarks.fetch(bm.guid);
    return moved?.parentGuid === folder.guid;
  }, "Cut bookmark is moved into the folder.");

  const moved = await PlacesUtils.bookmarks.fetch(bm.guid);
  isnot(
    moved.parentGuid,
    PlacesUtils.bookmarks.toolbarGuid,
    "The bookmark is no longer in its original parent."
  );

  Services.clipboard.emptyClipboard(Ci.nsIClipboard.kGlobalClipboard);
  await PlacesUtils.bookmarks.eraseEverything();
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_delete_via_context_menu() {
  const bookmark = await addBookmark({ title: "Delete Me" });

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;

  const row = await getBookmarkRow(tabList, bookmark);

  const contextMenu = SidebarController.currentContextMenu;
  const promiseRemoved =
    PlacesTestUtils.waitForNotification("bookmark-removed");
  await openAndWaitForContextMenu(contextMenu, row.mainEl, () =>
    contextMenu.activateItem(
      document.getElementById("sidebar-bookmarks-context-delete-bookmark")
    )
  );
  await promiseRemoved;

  await BrowserTestUtils.waitForMutationCondition(
    tabList.shadowRoot,
    { childList: true, subtree: true },
    () => !row.isConnected
  );

  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_empty_folder_shows_label() {
  const folder = await addFolder("Empty Folder");

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;

  const toolbarDetails = await openToolbarFolder(tabList);

  const nestedList = toolbarDetails.querySelector("sidebar-bookmark-list");
  await BrowserTestUtils.waitForMutationCondition(
    nestedList.shadowRoot,
    { childList: true, subtree: true },
    () => nestedList.folderLabelEl
  );

  ok(nestedList.folderLabelEl, "Empty folder renders as a label element.");
  Assert.equal(
    nestedList.folderLabelEl.textContent.trim(),
    "Empty Folder",
    "Empty folder label text matches."
  );
  Assert.equal(
    nestedList.folderLabelEl.getAttribute("role"),
    "treeitem",
    "Empty folder has a treeitem role."
  );
  Assert.equal(
    nestedList.folderLabelEl.getAttribute("aria-label"),
    "Empty Folder",
    "Empty folder has an accessible name matching its title."
  );

  await PlacesUtils.bookmarks.remove({ guid: folder.guid });
  SidebarTestUtils.closePanel(window);
});
