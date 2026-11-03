/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { showBookmarksSidebar, openFolder, openToolbarFolder } =
  SidebarTestUtils.bookmarks;

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

/**
 * Creates a Recently Bookmarked query and a Recent Tags query with one tag.
 *
 * @param {string} url - The URL used for the query result.
 * @param {string} tag - The tag used for the tags-root query.
 * @returns {object} The created bookmarks for use and cleanup.
 */
async function createSmartBookmarks(url, tag) {
  // "Recently Bookmarked" style smart bookmark.
  const recentBookmark = await addBookmark({ title: "Recent Page", url });
  const recentSmart = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "Recently Bookmarked",
    url: "place:queryType=1&sort=12&maxResults=10&excludeQueries=1&excludeItemIfParentHasAnnotation=livemark%2FfeedURI",
  });

  // "Recent Tags" style smart bookmark with one bookmark tagged so the
  // RESULTS_AS_TAGS_ROOT query yields a tag container.
  await PlacesUtils.tagging.tagURI(Services.io.newURI(url), [tag]);
  const tagsSmart = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "Recent Tags",
    url: `place:type=${Ci.nsINavHistoryQueryOptions.RESULTS_AS_TAGS_ROOT}&sort=1&maxResults=10`,
  });
  return { recentBookmark, recentSmart, tagsSmart };
}

/**
 * Removes the smart-bookmark fixture created by createSmartBookmarks().
 *
 * @param {object} smartBookmarks - The fixture returned by createSmartBookmarks().
 * @param {object} smartBookmarks.recentBookmark - The regular tagged bookmark.
 * @param {object} smartBookmarks.recentSmart - The Recently Bookmarked query.
 * @param {object} smartBookmarks.tagsSmart - The Recent Tags query.
 * @param {string} url - The URL to untag.
 * @param {string} tag - The tag to remove.
 */
async function removeSmartBookmarks(
  { recentBookmark, recentSmart, tagsSmart },
  url,
  tag
) {
  await PlacesUtils.tagging.untagURI(Services.io.newURI(url), [tag]);
  await PlacesUtils.bookmarks.remove(tagsSmart.guid);
  await PlacesUtils.bookmarks.remove(recentSmart.guid);
  await PlacesUtils.bookmarks.remove(recentBookmark.guid);
}

add_task(async function test_bookmarks_smart_bookmark_renders_as_folder() {
  const url = "https://example.com/recent-smart";
  const tag = "regression-tag";
  const smartBookmarks = await createSmartBookmarks(url, tag);
  const { recentSmart, tagsSmart } = smartBookmarks;

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;

  const toolbarDetails = await openToolbarFolder(tabList);
  const toolbarList = toolbarDetails.querySelector("sidebar-bookmark-list");

  await BrowserTestUtils.waitForMutationCondition(
    toolbarList.shadowRoot,
    { childList: true, subtree: true },
    () =>
      [...toolbarList.folderEls].some(
        d => d.guid === recentSmart.guid || d.guid === tagsSmart.guid
      )
  );

  const recentFolder = [...toolbarList.folderEls].find(
    d => d.guid === recentSmart.guid
  );
  ok(recentFolder, "Recently Bookmarked renders as a folder.");
  Assert.equal(
    recentFolder.dataset.folderKind,
    "place-container",
    "Recently Bookmarked has the place-container folder kind."
  );

  const tagsFolder = [...toolbarList.folderEls].find(
    d => d.guid === tagsSmart.guid
  );
  ok(tagsFolder, "Recent Tags renders as a folder.");
  Assert.equal(
    tagsFolder.dataset.folderKind,
    "tags-root",
    "Recent Tags has the tags-root folder kind."
  );

  await openFolder(tagsFolder);

  const tagsList = tagsFolder.querySelector("sidebar-bookmark-list");
  await BrowserTestUtils.waitForMutationCondition(
    tagsList.shadowRoot,
    { childList: true, subtree: true },
    () => [...tagsList.folderEls].length
  );

  const tagContainers = [...tagsList.folderEls];
  Assert.greater(
    tagContainers.length,
    0,
    "Recent Tags renders each tag as a child folder."
  );
  ok(
    tagContainers.every(d => d.dataset.folderKind === "tag-container"),
    "Tag containers under Recent Tags are marked with the tag-container kind."
  );

  await removeSmartBookmarks(smartBookmarks, url, tag);
  SidebarController.hide();
});

add_task(async function test_bookmarks_smart_bookmark_uses_bookmark_guid() {
  // A folder-shortcut smart bookmark expands into the contents of a folder.
  // The expanded children must carry the bookmark guid (b.guid), not the page
  // guid (h.guid), otherwise selecting/editing/removing them targets the wrong
  // item.
  const folder = await addFolder("Shortcut Target");
  const innerBookmark = await addBookmark({
    title: "Inner Page",
    url: "https://example.com/inner-shortcut",
    parentGuid: folder.guid,
  });
  const { guid: pageGuid } = await PlacesUtils.history.fetch(
    "https://example.com/inner-shortcut"
  );
  Assert.notEqual(
    pageGuid,
    innerBookmark.guid,
    "Sanity check: page guid and bookmark guid differ."
  );

  const shortcut = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "Folder Shortcut",
    url: `place:parent=${folder.guid}`,
  });

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;

  const toolbarDetails = await openToolbarFolder(tabList);
  const toolbarList = toolbarDetails.querySelector("sidebar-bookmark-list");

  await BrowserTestUtils.waitForMutationCondition(
    toolbarList.shadowRoot,
    { childList: true, subtree: true },
    () => [...toolbarList.folderEls].some(d => d.guid === shortcut.guid)
  );

  const shortcutFolder = [...toolbarList.folderEls].find(
    d => d.guid === shortcut.guid
  );
  ok(shortcutFolder, "Folder shortcut renders as a folder.");

  if (!shortcutFolder.open) {
    shortcutFolder.querySelector("summary").click();
    await BrowserTestUtils.waitForMutationCondition(
      shortcutFolder,
      { attributes: true },
      () => shortcutFolder.open
    );
  }

  const shortcutList = shortcutFolder.querySelector("sidebar-bookmark-list");
  const innerRow = await BrowserTestUtils.waitForMutationCondition(
    shortcutList.shadowRoot,
    { childList: true, subtree: true },
    () => [...shortcutList.rowEls].find(row => row.title === "Inner Page")
  );
  ok(innerRow, "The folder's bookmark is visible inside the shortcut.");
  Assert.equal(
    innerRow.guid,
    innerBookmark.guid,
    "Expanded child carries the bookmark guid, not the page guid."
  );

  await PlacesUtils.bookmarks.remove(shortcut.guid);
  await PlacesUtils.bookmarks.remove(innerBookmark.guid);
  await PlacesUtils.bookmarks.remove(folder.guid);
  SidebarController.hide();
});

add_task(async function test_bookmarks_folder_shortcut_uses_folder_icon() {
  // A shortcut to a plain folder should render with a plain folder icon,
  // not the tag-container icon (bug 2047956).
  const folder = await addFolder("Icon Shortcut Target");
  const innerBookmark = await addBookmark({
    title: "Inner Page",
    url: "https://example.com/folder-shortcut-icon-inner",
    parentGuid: folder.guid,
  });
  const folderShortcut = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "Folder Shortcut Icon Test",
    url: `place:parent=${folder.guid}`,
  });
  // A shortcut to an actual tag container should still render with the
  // tag-container icon.
  const taggedBookmark = await addBookmark({
    title: "Tagged Page",
    url: "https://example.com/folder-shortcut-icon-test",
  });
  await PlacesUtils.tagging.tagURI(
    Services.io.newURI("https://example.com/folder-shortcut-icon-test"),
    ["icon-test-tag"]
  );
  const tagShortcut = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "Tag Shortcut Icon Test",
    url: "place:tag=icon-test-tag",
  });

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;
  const toolbarDetails = await openToolbarFolder(tabList);
  const toolbarList = toolbarDetails.querySelector("sidebar-bookmark-list");
  await BrowserTestUtils.waitForMutationCondition(
    toolbarList.shadowRoot,
    { childList: true, subtree: true },
    () =>
      [...toolbarList.folderEls].some(d => d.guid === folderShortcut.guid) &&
      [...toolbarList.folderEls].some(d => d.guid === tagShortcut.guid)
  );

  const folderShortcutEl = [...toolbarList.folderEls].find(
    d => d.guid === folderShortcut.guid
  );
  ok(folderShortcutEl, "Folder shortcut renders as a folder.");
  Assert.equal(
    folderShortcutEl.dataset.folderKind,
    "place-container",
    "Folder shortcut has the place-container folder kind, not tag-container."
  );

  const tagShortcutEl = [...toolbarList.folderEls].find(
    d => d.guid === tagShortcut.guid
  );
  ok(tagShortcutEl, "Tag shortcut renders as a folder.");
  Assert.equal(
    tagShortcutEl.dataset.folderKind,
    "tag-container",
    "Tag shortcut still has the tag-container folder kind."
  );

  await PlacesUtils.tagging.untagURI(
    Services.io.newURI("https://example.com/folder-shortcut-icon-test"),
    ["icon-test-tag"]
  );
  await PlacesUtils.bookmarks.remove(tagShortcut.guid);
  await PlacesUtils.bookmarks.remove(taggedBookmark.guid);
  await PlacesUtils.bookmarks.remove(folderShortcut.guid);
  await PlacesUtils.bookmarks.remove(innerBookmark.guid);
  await PlacesUtils.bookmarks.remove(folder.guid);
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_search_skips_smart_bookmark_results() {
  const url = "https://example.com/recent-smart-search";
  const tag = "regression-tag-search";
  const smartBookmarks = await createSmartBookmarks(url, tag);
  const { recentBookmark } = smartBookmarks;
  const folder = await addFolder("Search Shortcut Target");
  const innerBookmark = await addBookmark({
    title: "Recent Page Shortcut Child",
    url: "https://example.com/recent-smart-search-inner",
    parentGuid: folder.guid,
  });
  const shortcut = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "Search Folder Shortcut",
    url: `place:parent=${folder.guid}`,
  });

  const { component, contentWindow } = await showBookmarksSidebar(window);
  EventUtils.synthesizeMouseAtCenter(component.searchInput, {}, contentWindow);
  EventUtils.sendString("Recent Page", contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.searchResults?.length > 0
  );

  Assert.deepEqual(
    component.searchResults.map(result => result.guid).sort(),
    [recentBookmark.guid, innerBookmark.guid].sort(),
    "Each bookmark appears once, without copies from smart bookmarks."
  );

  await PlacesUtils.bookmarks.remove(shortcut.guid);
  await PlacesUtils.bookmarks.remove(innerBookmark.guid);
  await PlacesUtils.bookmarks.remove(folder.guid);
  await removeSmartBookmarks(smartBookmarks, url, tag);
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_smart_bookmark_drag_disabled() {
  const url = "https://example.com/recent-smart-drag";
  const tag = "regression-tag-drag";
  const smartBookmarks = await createSmartBookmarks(url, tag);
  const { recentSmart, tagsSmart } = smartBookmarks;

  // A regular bookmark used as the drag source for the drop-into attempts.
  const dragSource = await addBookmark({
    title: "Drag Source For Smart",
    url: "https://example.com/drag-source-smart",
  });

  const { component, contentWindow } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;

  const toolbarDetails = await openToolbarFolder(tabList);
  const toolbarList = toolbarDetails.querySelector("sidebar-bookmark-list");
  await BrowserTestUtils.waitForMutationCondition(
    toolbarList.shadowRoot,
    { childList: true, subtree: true },
    () =>
      [...toolbarList.folderEls].some(d => d.guid === recentSmart.guid) &&
      [...toolbarList.folderEls].some(d => d.guid === tagsSmart.guid) &&
      [...toolbarList.rowEls].some(r => r.guid === dragSource.guid)
  );

  const dispatchDragStart = el => {
    const event = new contentWindow.DragEvent("dragstart", {
      bubbles: true,
      cancelable: true,
      composed: true,
      dataTransfer: new contentWindow.DataTransfer(),
    });
    el.dispatchEvent(event);
    return event;
  };

  info("A regular bookmark row can still be dragged.");
  const sourceRow = [...toolbarList.rowEls].find(
    r => r.guid === dragSource.guid
  );
  ok(
    !dispatchDragStart(sourceRow).defaultPrevented,
    "dragstart is allowed for a regular bookmark."
  );

  info("Items within Recently Bookmarked cannot be dragged.");
  const recentFolder = [...toolbarList.folderEls].find(
    d => d.guid === recentSmart.guid
  );
  await openFolder(recentFolder);
  const recentList = recentFolder.querySelector("sidebar-bookmark-list");
  await BrowserTestUtils.waitForMutationCondition(
    recentList.shadowRoot,
    { childList: true, subtree: true },
    () => [...recentList.rowEls].length
  );
  ok(recentList.readOnly, "Recently Bookmarked contents are read-only.");
  const recentChildRow = [...recentList.rowEls][0];
  ok(
    dispatchDragStart(recentChildRow).defaultPrevented,
    "dragstart is blocked for an item within Recently Bookmarked."
  );

  info("Dropping onto Recently Bookmarked does not move the bookmark into it.");
  const recentSummary = recentFolder.querySelector("summary");
  const recentRect = recentSummary.getBoundingClientRect();
  EventUtils.synthesizeDrop(
    sourceRow,
    recentSummary,
    null,
    "move",
    contentWindow,
    contentWindow,
    {
      clientX: recentRect.left + recentRect.width / 2,
      clientY: recentRect.top + recentRect.height * 0.5,
      _domDispatchOnly: true,
    }
  );
  let sourceInfo = await PlacesUtils.bookmarks.fetch(dragSource.guid);
  Assert.equal(
    sourceInfo.parentGuid,
    PlacesUtils.bookmarks.toolbarGuid,
    "Bookmark is not moved into the Recently Bookmarked query folder."
  );

  info("Tag subfolders within Recent Tags cannot be dragged.");
  const tagsFolder = [...toolbarList.folderEls].find(
    d => d.guid === tagsSmart.guid
  );
  await openFolder(tagsFolder);
  const tagsList = tagsFolder.querySelector("sidebar-bookmark-list");
  await BrowserTestUtils.waitForMutationCondition(
    tagsList.shadowRoot,
    { childList: true, subtree: true },
    () => [...tagsList.folderEls].length
  );
  ok(tagsList.readOnly, "Recent Tags contents are read-only.");
  const tagContainer = [...tagsList.folderEls][0];
  ok(
    dispatchDragStart(tagContainer.querySelector("summary")).defaultPrevented,
    "dragstart is blocked for a tag subfolder within Recent Tags."
  );

  info("Dropping onto Recent Tags does not move the bookmark into it.");
  const tagsSummary = tagsFolder.querySelector("summary");
  const tagsRect = tagsSummary.getBoundingClientRect();
  EventUtils.synthesizeDrop(
    sourceRow,
    tagsSummary,
    null,
    "move",
    contentWindow,
    contentWindow,
    {
      clientX: tagsRect.left + tagsRect.width / 2,
      clientY: tagsRect.top + tagsRect.height * 0.5,
      _domDispatchOnly: true,
    }
  );
  sourceInfo = await PlacesUtils.bookmarks.fetch(dragSource.guid);
  Assert.equal(
    sourceInfo.parentGuid,
    PlacesUtils.bookmarks.toolbarGuid,
    "Bookmark is not moved into the Recent Tags query folder."
  );

  await removeSmartBookmarks(smartBookmarks, url, tag);
  await PlacesUtils.bookmarks.remove(dragSource.guid);
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_bookmarks_smart_bookmark_context_menu() {
  const url = "https://example.com/recent-smart-menu";
  const tag = "regression-tag-menu";
  const smartBookmarks = await createSmartBookmarks(url, tag);
  const { recentSmart, tagsSmart } = smartBookmarks;

  const { component } = await showBookmarksSidebar(window);
  const tabList = component.bookmarkList;

  const toolbarDetails = await openToolbarFolder(tabList);
  const toolbarList = toolbarDetails.querySelector("sidebar-bookmark-list");

  await BrowserTestUtils.waitForMutationCondition(
    toolbarList.shadowRoot,
    { childList: true, subtree: true },
    () =>
      [...toolbarList.folderEls].some(d => d.guid === recentSmart.guid) &&
      [...toolbarList.folderEls].some(d => d.guid === tagsSmart.guid)
  );

  const contextMenu = SidebarController.currentContextMenu;
  const item = id => document.getElementById(id);
  const hideMenu = async () => {
    const promiseHidden = BrowserTestUtils.waitForPopupEvent(
      contextMenu,
      "hidden"
    );
    contextMenu.hidePopup();
    await promiseHidden;
  };

  info(
    "Context menu on a place-container smart bookmark (Recently Bookmarked)."
  );
  const recentFolder = [...toolbarList.folderEls].find(
    d => d.guid === recentSmart.guid
  );
  await openAndWaitForContextMenu(
    contextMenu,
    recentFolder.querySelector("summary"),
    () => {}
  );

  ok(
    !item("sidebar-bookmarks-context-open-all-bookmarks").hidden,
    "Open all is visible for a place-container smart bookmark."
  );
  ok(
    !item("sidebar-bookmarks-context-open-all-bookmarks").disabled,
    "Open all is enabled for a non-empty place-container smart bookmark."
  );
  Assert.equal(
    item("sidebar-bookmarks-context-open-all-bookmarks").getAttribute(
      "data-l10n-id"
    ),
    "places-open-all-in-tabs",
    "Place-container uses the open-all-in-tabs label."
  );
  ok(
    !item("sidebar-bookmarks-context-edit-bookmark").hidden,
    "Edit is visible for a place-container smart bookmark."
  );
  ok(
    item("sidebar-bookmarks-context-edit-bookmark").disabled,
    "Edit is disabled for a place-container smart bookmark."
  );
  ok(
    !item("sidebar-bookmarks-context-delete-bookmark").hidden,
    "Delete is visible for a place-container smart bookmark."
  );
  Assert.equal(
    item("sidebar-bookmarks-context-delete-bookmark").getAttribute(
      "data-l10n-id"
    ),
    "text-action-delete",
    "Delete uses the generic delete label."
  );
  ok(
    !item("sidebar-bookmarks-context-cut").hidden,
    "Cut is visible for a place-container smart bookmark."
  );
  for (const id of [
    "sidebar-bookmarks-context-sort-by-name",
    "sidebar-bookmarks-context-show-in-folder",
    "sidebar-bookmarks-context-copy-link",
    "sidebar-bookmarks-context-paste",
    "sidebar-bookmarks-context-add-bookmark",
    "sidebar-bookmarks-context-add-folder",
    "sidebar-bookmarks-context-add-separator",
  ]) {
    ok(item(id).hidden, `${id} is hidden for a smart bookmark.`);
  }
  await hideMenu();

  info("Context menu on a tags-root smart bookmark (Recent Tags).");
  const tagsFolder = [...toolbarList.folderEls].find(
    d => d.guid === tagsSmart.guid
  );
  await openAndWaitForContextMenu(
    contextMenu,
    tagsFolder.querySelector("summary"),
    () => {}
  );
  ok(
    !item("sidebar-bookmarks-context-open-all-bookmarks").hidden,
    "Open all is visible for a tags-root smart bookmark."
  );
  ok(
    item("sidebar-bookmarks-context-open-all-bookmarks").disabled,
    "Open all is disabled for a tags-root smart bookmark."
  );
  ok(
    item("sidebar-bookmarks-context-edit-bookmark").disabled,
    "Edit is disabled for a tags-root smart bookmark."
  );
  await hideMenu();

  info("Context menu on a tag container (a child tag under Recent Tags).");
  await openFolder(tagsFolder);
  const tagsList = tagsFolder.querySelector("sidebar-bookmark-list");
  await BrowserTestUtils.waitForMutationCondition(
    tagsList.shadowRoot,
    { childList: true, subtree: true },
    () => [...tagsList.folderEls].length
  );
  const tagContainer = [...tagsList.folderEls][0];
  await openAndWaitForContextMenu(
    contextMenu,
    tagContainer.querySelector("summary"),
    () => {}
  );
  Assert.equal(
    item("sidebar-bookmarks-context-open-all-bookmarks").getAttribute(
      "data-l10n-id"
    ),
    "places-open-all-bookmarks",
    "Tag container uses the open-all-bookmarks label."
  );
  ok(
    !item("sidebar-bookmarks-context-edit-bookmark").disabled,
    "Edit is enabled for a tag container."
  );
  ok(
    item("sidebar-bookmarks-context-cut").hidden,
    "Cut is hidden for a tag container."
  );
  await hideMenu();

  await removeSmartBookmarks(smartBookmarks, url, tag);
  SidebarController.hide();
});
