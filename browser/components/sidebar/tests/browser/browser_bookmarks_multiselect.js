/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);

const UPDATED_BOOKMARKS_PREF = "sidebar.updatedBookmarks.enabled";

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [[UPDATED_BOOKMARKS_PREF, true]],
  });
  await PlacesUtils.bookmarks.eraseEverything();
  registerCleanupFunction(async () => {
    await PlacesUtils.bookmarks.eraseEverything();
    SidebarController.hide();
  });
});

async function showBookmarksSidebar() {
  if (SidebarController.currentID !== "viewBookmarksSidebar") {
    await SidebarTestUtils.showPanel(window, "viewBookmarksSidebar");
  }
  const { contentDocument, contentWindow } = SidebarController.browser;
  await BrowserTestUtils.waitForCondition(
    () => contentDocument.querySelector("sidebar-bookmarks"),
    "Wait for sidebar-bookmarks element"
  );
  const component = contentDocument.querySelector("sidebar-bookmarks");
  await component.updateComplete;
  return { component, contentWindow };
}

async function addBookmark({ url, title, parentGuid } = {}) {
  return PlacesUtils.bookmarks.insert({
    url,
    title,
    parentGuid: parentGuid ?? PlacesUtils.bookmarks.toolbarGuid,
  });
}

async function openToolbarFolder(tabList) {
  await BrowserTestUtils.waitForMutationCondition(
    tabList.shadowRoot,
    { childList: true, subtree: true },
    () => tabList.folderEls[0]
  );
  const toolbarDetails = tabList.folderEls[0];
  if (!toolbarDetails.open) {
    toolbarDetails.querySelector("summary").click();
    await BrowserTestUtils.waitForMutationCondition(
      toolbarDetails,
      { attributes: true },
      () => toolbarDetails.open
    );
  }
  return toolbarDetails;
}

async function getNestedList(toolbarDetails) {
  const nestedList = toolbarDetails.querySelector("sidebar-bookmark-list");
  await BrowserTestUtils.waitForMutationCondition(
    nestedList.shadowRoot,
    { childList: true, subtree: true },
    () => nestedList.rowEls.length
  );
  return nestedList;
}

async function clickRow(row, event = {}, contentWindow) {
  AccessibilityUtils.setEnv({ focusableRule: false });
  EventUtils.synthesizeMouseAtCenter(row.mainEl, event, contentWindow);
  AccessibilityUtils.resetEnv();
}

function getSelectedRows(list) {
  return [...list.rowEls].filter(row => row.selected);
}

add_task(async function test_accel_click_selects_row() {
  await addBookmark({ url: "https://example.com/", title: "Bookmark A" });
  await addBookmark({ url: "https://example.org/", title: "Bookmark B" });
  await addBookmark({ url: "https://example.net/", title: "Bookmark C" });

  const { component, contentWindow } = await showBookmarksSidebar();
  const tabList = component.bookmarkList;
  const toolbarDetails = await openToolbarFolder(tabList);
  const nestedList = await getNestedList(toolbarDetails);
  const rows = [...nestedList.rowEls];

  Assert.equal(
    getSelectedRows(nestedList).length,
    0,
    "No rows selected initially."
  );

  info("Accel + Click first row to select it.");
  await clickRow(rows[0], { accelKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[0],
    { attributes: true },
    () => rows[0].selected
  );
  Assert.equal(getSelectedRows(nestedList).length, 1, "One row selected.");
  Assert.ok(rows[0].selected, "First row is selected.");

  info("Accel + Click second row to add it to selection.");
  await clickRow(rows[1], { accelKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[1],
    { attributes: true },
    () => rows[1].selected
  );
  Assert.equal(getSelectedRows(nestedList).length, 2, "Two rows selected.");
  Assert.ok(rows[0].selected, "First row is still selected.");
  Assert.ok(rows[1].selected, "Second row is selected.");

  component.treeView.resetSelection();
  SidebarController.hide();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_accel_click_deselects_row() {
  await addBookmark({ url: "https://example.com/", title: "Bookmark A" });
  await addBookmark({ url: "https://example.org/", title: "Bookmark B" });

  const { component, contentWindow } = await showBookmarksSidebar();
  const tabList = component.bookmarkList;
  const toolbarDetails = await openToolbarFolder(tabList);
  const nestedList = await getNestedList(toolbarDetails);
  const rows = [...nestedList.rowEls];

  info("Accel + Click first row to select it.");
  await clickRow(rows[0], { accelKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[0],
    { attributes: true },
    () => rows[0].selected
  );
  Assert.ok(rows[0].selected, "First row is selected.");

  info("Accel + Click first row again to deselect it.");
  await clickRow(rows[0], { accelKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[0],
    { attributes: true },
    () => !rows[0].selected
  );
  Assert.ok(!rows[0].selected, "First row is deselected.");
  Assert.equal(getSelectedRows(nestedList).length, 0, "No rows selected.");

  component.treeView.resetSelection();
  SidebarController.hide();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_shift_click_selects_range() {
  await addBookmark({ url: "https://example.com/1", title: "Bookmark 1" });
  await addBookmark({ url: "https://example.com/2", title: "Bookmark 2" });
  await addBookmark({ url: "https://example.com/3", title: "Bookmark 3" });
  await addBookmark({ url: "https://example.com/4", title: "Bookmark 4" });

  const { component, contentWindow } = await showBookmarksSidebar();
  const tabList = component.bookmarkList;
  const toolbarDetails = await openToolbarFolder(tabList);
  const nestedList = await getNestedList(toolbarDetails);
  const rows = [...nestedList.rowEls];

  Assert.greaterOrEqual(rows.length, 4, "At least 4 rows rendered.");

  info("Click first row to set anchor.");
  await clickRow(rows[0], { accelKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[0],
    { attributes: true },
    () => rows[0].selected
  );

  info("Shift + Click third row to select range.");
  await clickRow(rows[2], { shiftKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[2],
    { attributes: true },
    () => rows[2].selected
  );

  Assert.ok(rows[0].selected, "First row is selected.");
  Assert.ok(rows[1].selected, "Second row is selected.");
  Assert.ok(rows[2].selected, "Third row is selected.");
  Assert.ok(!rows[3].selected, "Fourth row is not selected.");
  Assert.equal(getSelectedRows(nestedList).length, 3, "Three rows selected.");

  component.treeView.resetSelection();
  SidebarController.hide();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_normal_click_clears_selection() {
  await addBookmark({ url: "https://example.com/1", title: "Bookmark 1" });
  await addBookmark({ url: "https://example.com/2", title: "Bookmark 2" });

  const { component, contentWindow } = await showBookmarksSidebar();
  const tabList = component.bookmarkList;
  const toolbarDetails = await openToolbarFolder(tabList);
  const nestedList = await getNestedList(toolbarDetails);
  const rows = [...nestedList.rowEls];

  info("Accel + Click first row to select it.");
  await clickRow(rows[0], { accelKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[0],
    { attributes: true },
    () => rows[0].selected
  );
  info("Accel + Click second row to add it.");
  await clickRow(rows[1], { accelKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[1],
    { attributes: true },
    () => rows[1].selected
  );
  Assert.equal(getSelectedRows(nestedList).length, 2, "Two rows selected.");

  info("Normal click on a row should clear selection.");
  await clickRow(rows[0], {}, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[0],
    { attributes: true },
    () => !rows[0].selected
  );
  Assert.equal(
    getSelectedRows(nestedList).length,
    0,
    "Selection cleared after normal click."
  );

  component.treeView.resetSelection();
  SidebarController.hide();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_space_key_toggles_selection() {
  await addBookmark({ url: "https://example.com/1", title: "Bookmark 1" });
  await addBookmark({ url: "https://example.com/2", title: "Bookmark 2" });

  const { component, contentWindow } = await showBookmarksSidebar();
  const tabList = component.bookmarkList;
  const toolbarDetails = await openToolbarFolder(tabList);
  const nestedList = await getNestedList(toolbarDetails);
  const rows = [...nestedList.rowEls];

  info("Focus the first row.");
  rows[0].focus();

  info("Press Space to select the focused row.");
  EventUtils.synthesizeKey(" ", {}, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[0],
    { attributes: true },
    () => rows[0].selected
  );
  Assert.ok(rows[0].selected, "First row is selected after Space.");
  Assert.equal(getSelectedRows(nestedList).length, 1, "One row selected.");

  info("Press Space again to deselect the row.");
  EventUtils.synthesizeKey(" ", {}, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[0],
    { attributes: true },
    () => !rows[0].selected
  );
  Assert.ok(!rows[0].selected, "First row is deselected after second Space.");
  Assert.equal(getSelectedRows(nestedList).length, 0, "No rows selected.");

  component.treeView.resetSelection();
  SidebarController.hide();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_shift_arrowdown_extends_selection() {
  await addBookmark({ url: "https://example.com/1", title: "Bookmark 1" });
  await addBookmark({ url: "https://example.com/2", title: "Bookmark 2" });
  await addBookmark({ url: "https://example.com/3", title: "Bookmark 3" });

  const { component, contentWindow } = await showBookmarksSidebar();
  const tabList = component.bookmarkList;
  const toolbarDetails = await openToolbarFolder(tabList);
  const nestedList = await getNestedList(toolbarDetails);
  const rows = [...nestedList.rowEls];

  Assert.greaterOrEqual(rows.length, 3, "At least 3 rows rendered.");

  info("Focus first row and select with Space.");
  rows[0].focus();
  EventUtils.synthesizeKey(" ", {}, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[0],
    { attributes: true },
    () => rows[0].selected
  );
  Assert.ok(rows[0].selected, "First row selected.");

  info("Shift + ArrowDown to extend selection to second row.");
  EventUtils.synthesizeKey("KEY_ArrowDown", { shiftKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[1],
    { attributes: true },
    () => rows[1].selected
  );
  Assert.ok(rows[0].selected, "First row still selected.");
  Assert.ok(rows[1].selected, "Second row selected after Shift+ArrowDown.");

  info("Shift + ArrowDown again to extend to third row.");
  EventUtils.synthesizeKey("KEY_ArrowDown", { shiftKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[2],
    { attributes: true },
    () => rows[2].selected
  );
  Assert.equal(
    getSelectedRows(nestedList).length,
    3,
    "All three rows selected."
  );

  component.treeView.resetSelection();
  SidebarController.hide();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_shift_arrowup_extends_selection() {
  await addBookmark({ url: "https://example.com/1", title: "Bookmark 1" });
  await addBookmark({ url: "https://example.com/2", title: "Bookmark 2" });
  await addBookmark({ url: "https://example.com/3", title: "Bookmark 3" });

  const { component, contentWindow } = await showBookmarksSidebar();
  const tabList = component.bookmarkList;
  const toolbarDetails = await openToolbarFolder(tabList);
  const nestedList = await getNestedList(toolbarDetails);
  const rows = [...nestedList.rowEls];

  info("Focus last row and select with Space.");
  rows.at(-1).focus();
  EventUtils.synthesizeKey(" ", {}, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows.at(-1),
    { attributes: true },
    () => rows.at(-1).selected
  );

  info("Shift + ArrowUp to extend selection upward.");
  EventUtils.synthesizeKey("KEY_ArrowUp", { shiftKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows.at(-2),
    { attributes: true },
    () => rows.at(-2).selected
  );
  Assert.ok(rows.at(-1).selected, "Last row still selected.");
  Assert.ok(
    rows.at(-2).selected,
    "Second-to-last row selected after Shift+ArrowUp."
  );
  Assert.equal(getSelectedRows(nestedList).length, 2, "Two rows selected.");

  component.treeView.resetSelection();
  SidebarController.hide();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_plain_arrow_clears_selection() {
  await addBookmark({ url: "https://example.com/1", title: "Bookmark 1" });
  await addBookmark({ url: "https://example.com/2", title: "Bookmark 2" });
  await addBookmark({ url: "https://example.com/3", title: "Bookmark 3" });

  const { component, contentWindow } = await showBookmarksSidebar();
  const tabList = component.bookmarkList;
  const toolbarDetails = await openToolbarFolder(tabList);
  const nestedList = await getNestedList(toolbarDetails);
  const rows = [...nestedList.rowEls];

  info("Accel + Click first and second rows to multiselect.");
  await clickRow(rows[0], { accelKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[0],
    { attributes: true },
    () => rows[0].selected
  );
  await clickRow(rows[1], { accelKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[1],
    { attributes: true },
    () => rows[1].selected
  );
  Assert.equal(getSelectedRows(nestedList).length, 2, "Two rows selected.");

  info("Focus first row and press plain ArrowDown — selection should clear.");
  rows[0].focus();
  EventUtils.synthesizeKey("KEY_ArrowDown", {}, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[0],
    { attributes: true },
    () => !rows[0].selected
  );
  Assert.equal(
    getSelectedRows(nestedList).length,
    0,
    "Selection cleared after plain ArrowDown."
  );

  component.treeView.resetSelection();
  SidebarController.hide();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_shift_click_across_nested_folders() {
  // Tests that we can multiselect rows in the following structure:
  // V Bookmarks Toolbar
  //   V Folder
  //     - Inner 1
  //     - Inner 2
  //   - Outer 1
  //   - Outer 2
  const folder = await PlacesUtils.bookmarks.insert({
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "Folder",
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
  });
  await addBookmark({
    url: "https://example.com/inner-1",
    title: "Inner 1",
    parentGuid: folder.guid,
  });
  await addBookmark({
    url: "https://example.com/inner-2",
    title: "Inner 2",
    parentGuid: folder.guid,
  });
  await addBookmark({ url: "https://example.com/outer-1", title: "Outer 1" });
  await addBookmark({ url: "https://example.com/outer-2", title: "Outer 2" });

  const { component, contentWindow } = await showBookmarksSidebar();
  const tabList = component.bookmarkList;
  const toolbarFolder = await openToolbarFolder(tabList);
  const toolbarList = await getNestedList(toolbarFolder);

  const folderEl = [...toolbarList.folderEls].find(
    el => el.guid === folder.guid
  );
  Assert.ok(folderEl, "Inner folder rendered.");
  if (!folderEl.open) {
    folderEl.open = true;
    await TestUtils.waitForCondition(
      () => toolbarList.expandedFolderGuids.has(folder.guid),
      "Folder open propagated to expandedFolderGuids."
    );
  }
  const innerList = await getNestedList(folderEl);

  const innerRows = [...innerList.rowEls];
  const outerRows = [...toolbarList.rowEls];

  info("Click first inner row.");
  await clickRow(innerRows[0], { accelKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    innerRows[0],
    { attributes: true },
    () => innerRows[0].selected
  );

  const lastOuter = outerRows.at(-1);
  info("Shift + Click last outer row.");
  await clickRow(lastOuter, { shiftKey: true }, contentWindow);
  await Promise.all(
    [...innerRows, ...outerRows].map(row =>
      BrowserTestUtils.waitForMutationCondition(
        row,
        { attributes: true },
        () => row.selected
      )
    )
  );

  component.treeView.resetSelection();
  SidebarController.hide();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_treeview_getSelectedTabItems() {
  await addBookmark({ url: "https://example.com/1", title: "Bookmark 1" });
  await addBookmark({ url: "https://example.com/2", title: "Bookmark 2" });
  await addBookmark({ url: "https://example.com/3", title: "Bookmark 3" });

  const { component, contentWindow } = await showBookmarksSidebar();
  const tabList = component.bookmarkList;
  const toolbarDetails = await openToolbarFolder(tabList);
  const nestedList = await getNestedList(toolbarDetails);
  const rows = [...nestedList.rowEls];

  info("Accel + Click first and third rows.");
  await clickRow(rows[0], { accelKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[0],
    { attributes: true },
    () => rows[0].selected
  );
  await clickRow(rows[2], { accelKey: true }, contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    rows[2],
    { attributes: true },
    () => rows[2].selected
  );

  const selectedItems = component.treeView.getSelectedTabItems();
  Assert.equal(selectedItems.length, 2, "getSelectedTabItems returns 2 items.");

  component.treeView.resetSelection();
  Assert.equal(
    component.treeView.getSelectedTabItems().length,
    0,
    "Selection cleared after resetSelection."
  );

  SidebarController.hide();
  await PlacesUtils.bookmarks.eraseEverything();
});
