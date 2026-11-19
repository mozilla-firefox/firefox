/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);

const { showBookmarksSidebar } = SidebarTestUtils.bookmarks;

const UPDATED_BOOKMARKS_PREF = "sidebar.updatedBookmarks.enabled";

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [[UPDATED_BOOKMARKS_PREF, true]],
  });
  await PlacesUtils.bookmarks.eraseEverything();
  await SidebarTestUtils.waitForInitialized(window);
  registerCleanupFunction(async () => {
    await PlacesUtils.bookmarks.eraseEverything();
    SidebarTestUtils.closePanel(window);
  });
});

add_task(async function test_bookmarks_drag_reorders_items() {
  const bmA = await addBookmark({
    title: "Drag First",
    url: "https://example.com/a",
  });
  const bmB = await addBookmark({
    title: "Drag Second",
    url: "https://example.com/b",
  });

  const { component, contentWindow } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;

  const rowA = await getBookmarkRow(tabList, bmA);
  const rowB = await getBookmarkRow(tabList, bmB);
  ok(rowA && rowB, "Both bookmark rows are visible.");

  let fetchA = await PlacesUtils.bookmarks.fetch(bmA.guid);
  let fetchB = await PlacesUtils.bookmarks.fetch(bmB.guid);
  Assert.less(fetchA.index, fetchB.index, "Bookmark A is before B initially.");

  const rectB = rowB.getBoundingClientRect();
  EventUtils.synthesizeDrop(
    rowA,
    rowB,
    null,
    "move",
    contentWindow,
    contentWindow,
    {
      clientX: rectB.left + rectB.width / 2,
      clientY: rectB.top + rectB.height * 0.75,
      _domDispatchOnly: true,
    }
  );

  await TestUtils.waitForCondition(async () => {
    fetchA = await PlacesUtils.bookmarks.fetch(bmA.guid);
    fetchB = await PlacesUtils.bookmarks.fetch(bmB.guid);
    return fetchA.index > fetchB.index;
  }, "Bookmark A moves after B.");

  await PlacesUtils.bookmarks.remove(bmA.guid);
  await PlacesUtils.bookmarks.remove(bmB.guid);
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_drag_into_folder() {
  const folder = await addFolder("Drop Target Folder");
  await addBookmark({
    title: "Inside Folder Already",
    parentGuid: folder.guid,
  });
  const bm = await addBookmark({
    title: "Drag To Folder",
    url: "https://example.com/drag",
  });

  const { component, contentWindow } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;

  const bookmarkRow = await getBookmarkRow(tabList, bm);
  const folderSummary = (
    await getBookmarkFolder(tabList, folder)
  ).querySelector("summary");
  ok(
    bookmarkRow && folderSummary,
    "Bookmark row and folder summary are found."
  );

  const rectSummary = folderSummary.getBoundingClientRect();
  EventUtils.synthesizeDrop(
    bookmarkRow,
    folderSummary,
    null,
    "move",
    contentWindow,
    contentWindow,
    {
      clientX: rectSummary.left + rectSummary.width / 2,
      clientY: rectSummary.top + rectSummary.height * 0.5,
      _domDispatchOnly: true,
    }
  );

  await TestUtils.waitForCondition(async () => {
    const fetchBm = await PlacesUtils.bookmarks.fetch(bm.guid);
    return fetchBm.parentGuid === folder.guid;
  }, "Bookmark is moved into the folder.");

  await PlacesUtils.bookmarks.remove({ guid: folder.guid });
  SidebarTestUtils.closePanel(window);
});

async function dropBookmarkOnFolderShortcut(targetFolderGuid) {
  await addBookmark({
    title: "Inside Shortcut Target",
    url: "https://example.com/inside-shortcut-target",
    parentGuid: targetFolderGuid,
  });
  const shortcut = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "Drop Target Shortcut",
    url: `place:parent=${targetFolderGuid}`,
  });
  const bm = await addBookmark({
    title: "Drag To Shortcut",
    url: "https://example.com/drag-to-shortcut",
  });

  const { component, contentWindow } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;

  const bookmarkRow = await getBookmarkRow(tabList, bm);
  const shortcutSummary = (
    await getBookmarkFolder(tabList, shortcut)
  ).querySelector("summary");
  ok(
    bookmarkRow && shortcutSummary,
    "Bookmark row and folder shortcut summary are found."
  );

  const rectSummary = shortcutSummary.getBoundingClientRect();
  EventUtils.synthesizeDrop(
    bookmarkRow,
    shortcutSummary,
    null,
    "move",
    contentWindow,
    contentWindow,
    {
      clientX: rectSummary.left + rectSummary.width / 2,
      clientY: rectSummary.top + rectSummary.height * 0.5,
      _domDispatchOnly: true,
    }
  );

  await TestUtils.waitForCondition(async () => {
    const fetchBm = await PlacesUtils.bookmarks.fetch(bm.guid);
    return fetchBm.parentGuid === targetFolderGuid;
  }, "Bookmark is moved into the folder the shortcut points to.");

  await PlacesUtils.bookmarks.eraseEverything();
  SidebarTestUtils.closePanel(window);
}

add_task(async function test_bookmarks_drag_into_folder_shortcut() {
  const folder = await addFolder("Shortcut Target Folder");
  await dropBookmarkOnFolderShortcut(folder.guid);
});

add_task(async function test_bookmarks_drag_into_root_folder_shortcut() {
  await dropBookmarkOnFolderShortcut(PlacesUtils.bookmarks.menuGuid);
});

/**
 * Creates a folder in Other Bookmarks holding the given bookmarks, adds a
 * shortcut to it in the toolbar, and opens the shortcut in the sidebar.
 *
 * @param {string[]} titles - Titles of the bookmarks to create in the folder.
 */
async function openFolderShortcutWithBookmarks(titles) {
  const folder = await addFolder(
    "Shortcut Contents Folder",
    PlacesUtils.bookmarks.unfiledGuid
  );
  const bookmarks = [];
  for (const [i, title] of titles.entries()) {
    bookmarks.push(
      await addBookmark({
        title,
        url: `https://example.com/shortcut-contents-${i}`,
        parentGuid: folder.guid,
      })
    );
  }
  const shortcut = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "Shortcut Contents",
    url: `place:parent=${folder.guid}`,
  });

  const { component, contentWindow } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;
  const shortcutDetails = await getBookmarkFolder(tabList, shortcut);
  await SidebarTestUtils.bookmarks.openFolder(shortcutDetails);
  const shortcutList = shortcutDetails.querySelector("sidebar-bookmark-list");
  const rows = [];
  for (const bookmark of bookmarks) {
    rows.push(
      await SidebarTestUtils.bookmarks.findBookmarkItemByGuid(
        shortcutList,
        "rowEls",
        bookmark.guid
      )
    );
  }
  return { folder, bookmarks, rows, tabList, contentWindow };
}

add_task(async function test_bookmarks_drag_reorders_within_folder_shortcut() {
  const {
    folder,
    bookmarks: [bmA, bmB],
    rows: [rowA, rowB],
    contentWindow,
  } = await openFolderShortcutWithBookmarks([
    "Shortcut First",
    "Shortcut Second",
  ]);
  ok(rowA && rowB, "Both bookmark rows are visible inside the shortcut.");

  const rectB = rowB.getBoundingClientRect();
  EventUtils.synthesizeDrop(
    rowA,
    rowB,
    null,
    "move",
    contentWindow,
    contentWindow,
    {
      clientX: rectB.left + rectB.width / 2,
      clientY: rectB.top + rectB.height * 0.75,
      _domDispatchOnly: true,
    }
  );

  await TestUtils.waitForCondition(async () => {
    const fetchA = await PlacesUtils.bookmarks.fetch(bmA.guid);
    const fetchB = await PlacesUtils.bookmarks.fetch(bmB.guid);
    return fetchA.index > fetchB.index;
  }, "Bookmark A moves after B within the shortcut's target folder.");
  Assert.equal(
    (await PlacesUtils.bookmarks.fetch(bmA.guid)).parentGuid,
    folder.guid,
    "Bookmark A stays in the shortcut's target folder."
  );

  await PlacesUtils.bookmarks.eraseEverything();
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_drag_to_position_in_folder_shortcut() {
  const bm = await addBookmark({
    title: "Drag Into Shortcut Position",
    url: "https://example.com/drag-into-shortcut-position",
  });
  const {
    folder,
    rows: [, lastRow],
    tabList,
    contentWindow,
  } = await openFolderShortcutWithBookmarks([
    "Shortcut First",
    "Shortcut Last",
  ]);
  const bookmarkRow = await getBookmarkRow(tabList, bm);
  ok(bookmarkRow && lastRow, "Source row and shortcut row are found.");

  const rect = lastRow.getBoundingClientRect();
  EventUtils.synthesizeDrop(
    bookmarkRow,
    lastRow,
    null,
    "move",
    contentWindow,
    contentWindow,
    {
      clientX: rect.left + rect.width / 2,
      clientY: rect.top + rect.height * 0.25,
      _domDispatchOnly: true,
    }
  );

  await TestUtils.waitForCondition(async () => {
    const fetchBm = await PlacesUtils.bookmarks.fetch(bm.guid);
    return fetchBm.parentGuid === folder.guid && fetchBm.index === 1;
  }, "Bookmark is dropped between the shortcut's two bookmarks.");

  await PlacesUtils.bookmarks.eraseEverything();
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_drag_hover_expands_folder() {
  const folder = await addFolder("Hover Expand Folder");
  await addBookmark({
    title: "Inside Hover Folder",
    parentGuid: folder.guid,
  });
  const bm = await addBookmark({
    title: "Hover Drag Source",
    url: "https://example.com/hover-drag",
  });

  const { component, contentWindow } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;

  const bookmarkRow = await getBookmarkRow(tabList, bm);
  const folderDetails = await getBookmarkFolder(tabList, folder);
  const folderSummary = folderDetails.querySelector("summary");
  ok(bookmarkRow && folderSummary, "Source row and target folder are found.");
  ok(!folderDetails.open, "Target folder starts collapsed.");

  EventUtils.startDragSession(contentWindow, "move");
  try {
    const rect = folderSummary.getBoundingClientRect();
    EventUtils.synthesizeDragOver(
      bookmarkRow,
      folderSummary,
      null,
      "move",
      contentWindow,
      contentWindow,
      {
        clientX: rect.left + rect.width / 2,
        clientY: rect.top + rect.height * 0.5,
        _domDispatchOnly: true,
      }
    );

    await TestUtils.waitForCondition(
      () => folderDetails.open,
      "Collapsed folder auto-expands while hovered during a drag."
    );

    const bmAfter = await PlacesUtils.bookmarks.fetch(bm.guid);
    Assert.equal(
      bmAfter.parentGuid,
      PlacesUtils.bookmarks.toolbarGuid,
      "Hovering must not move the dragged bookmark."
    );
  } finally {
    const sess = contentWindow.windowUtils.dragSession;
    if (sess) {
      sess.endDragSession(true);
    }
  }

  await PlacesUtils.bookmarks.remove({ guid: folder.guid });
  await PlacesUtils.bookmarks.remove({ guid: bm.guid });
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_drag_url_to_panel() {
  const folder = await addFolder("URL Drop Target Folder");
  await addBookmark({
    title: "Existing Bookmark",
    parentGuid: folder.guid,
  });

  const { component, contentWindow } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;

  const folderSummary = (
    await getBookmarkFolder(tabList, folder)
  ).querySelector("summary");
  ok(folderSummary, "Drop target folder summary found.");

  const rectSummary = folderSummary.getBoundingClientRect();
  const promiseAdded = PlacesTestUtils.waitForNotification("bookmark-added");
  // Use the browser tab as a neutral external drag source so the sidebar's
  // own dragstart handler does not add TYPE_X_MOZ_PLACE data that would take
  // priority over the explicit text/x-moz-url data.
  EventUtils.synthesizeDrop(
    gBrowser.selectedTab,
    folderSummary,
    [
      [
        {
          type: "text/x-moz-url",
          data: "https://example.com/dropped\nDropped URL",
        },
      ],
    ],
    "copy",
    window,
    contentWindow,
    {
      clientX: rectSummary.left + rectSummary.width / 2,
      clientY: rectSummary.top + rectSummary.height * 0.5,
      _domDispatchOnly: true,
    }
  );

  await promiseAdded;

  const fetchInfo = await PlacesUtils.bookmarks.fetch({
    url: "https://example.com/dropped",
  });
  ok(fetchInfo, "Dropped URL was bookmarked.");
  Assert.equal(
    fetchInfo.parentGuid,
    folder.guid,
    "Dropped URL bookmark is in the target folder."
  );

  await PlacesUtils.bookmarks.remove({ guid: folder.guid });
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_drag_tab_to_panel() {
  const bm = await addBookmark({ title: "Tab Drop Target" });
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/tab-page"
  );

  const { component, contentWindow } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;

  const targetRow = await getBookmarkRow(tabList, bm);
  ok(targetRow, "Drop target bookmark row found.");

  const rectRow = targetRow.getBoundingClientRect();
  const promiseAdded = PlacesTestUtils.waitForNotification("bookmark-added");
  EventUtils.synthesizeDrop(
    tab,
    targetRow,
    null,
    "copy",
    window,
    contentWindow,
    {
      clientX: rectRow.left + rectRow.width / 2,
      clientY: rectRow.top + rectRow.height * 0.75,
      _domDispatchOnly: true,
    }
  );

  await promiseAdded;

  const fetchInfo = await PlacesUtils.bookmarks.fetch({
    url: "https://example.com/tab-page",
  });
  ok(fetchInfo, "Tab page was bookmarked after drag.");
  Assert.equal(
    fetchInfo.parentGuid,
    PlacesUtils.bookmarks.toolbarGuid,
    "Tab bookmark is in the toolbar folder."
  );

  await PlacesUtils.bookmarks.remove(fetchInfo.guid);
  BrowserTestUtils.removeTab(tab);
  await PlacesUtils.bookmarks.remove(bm.guid);
  SidebarTestUtils.closePanel(window);
});
