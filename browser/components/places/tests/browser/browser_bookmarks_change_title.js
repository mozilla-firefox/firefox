/**
 * Tests that the title of a bookmark can be changed from the bookmark star, toolbar, sidebar, and library.
 */
"use strict";

const TEST_URL = "about:buildconfig";
const titleAfterFirstUpdate = "BookmarkStar title";

add_setup(async function () {
  await ensureBookmarksToolbarIsVisibleAndPopulated();
});

add_task(async function test_change_title_from_BookmarkStar() {
  let originalBm = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: TEST_URL,
    title: "Before Edit",
  });

  const win = await BrowserTestUtils.openNewBrowserWindow();
  await BrowserTestUtils.openNewForegroundTab({
    gBrowser: win.gBrowser,
    opening: TEST_URL,
    waitForStateStop: true,
  });

  win.StarUI._createPanelIfNeeded();
  let bookmarkPanel = win.document.getElementById("editBookmarkPanel");
  let shownPromise = promisePopupShown(bookmarkPanel);

  let bookmarkStar = win.BookmarkingUI.star;
  bookmarkStar.click();

  await shownPromise;

  let bookmarkPanelTitle = win.document.getElementById(
    "editBookmarkPanelTitle"
  );
  await TestUtils.waitForCondition(
    () =>
      bookmarkPanelTitle.textContent ===
      gFluentStrings.formatValueSync("bookmarks-edit-bookmark"),
    "Wait until the bookmark panel title will be changed expectedly."
  );
  Assert.ok(true, "Bookmark title is correct");

  let promiseNotification = PlacesTestUtils.waitForNotification(
    "bookmark-title-changed",
    events => events.some(e => e.title === titleAfterFirstUpdate)
  );

  // Update the bookmark's title.
  await fillBookmarkTextField(
    "editBMPanel_namePicker",
    titleAfterFirstUpdate,
    win
  );

  let doneButton = win.document.getElementById("editBookmarkPanelDoneButton");
  doneButton.click();
  await promiseNotification;

  let updatedBm = await PlacesUtils.bookmarks.fetch(originalBm.guid);
  Assert.equal(
    updatedBm.title,
    titleAfterFirstUpdate,
    "Should have updated the bookmark title in the database"
  );
  await PlacesUtils.bookmarks.remove(originalBm.guid);
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_change_title_from_Toolbar() {
  await SimpleTest.promiseFocus(window);
  let toolbarBookmark = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: titleAfterFirstUpdate,
    url: TEST_URL,
  });

  let toolbarNode = getToolbarNodeForItemGuid(toolbarBookmark.guid);

  await withBookmarksDialog(
    false,
    async function openPropertiesDialog() {
      let placesContext = document.getElementById("placesContext");
      let promisePopup = BrowserTestUtils.waitForEvent(
        placesContext,
        "popupshown"
      );
      EventUtils.synthesizeMouseAtCenter(toolbarNode, {
        button: 2,
        type: "contextmenu",
      });
      await promisePopup;

      let properties = document.getElementById(
        "placesContext_show_bookmark:info"
      );
      placesContext.activateItem(properties, {});
    },
    async function test(dialogWin) {
      // Ensure the dialog has initialized.
      await TestUtils.waitForCondition(() => dialogWin.document.title);

      let namepicker = dialogWin.document.getElementById(
        "editBMPanel_namePicker"
      );

      let editBookmarkDialogTitle =
        dialogWin.document.getElementById("titleText");

      Assert.equal(
        await dialogWin.document.l10n.formatValue(
          "bookmark-properties-title-edit-bookmark"
        ),
        editBookmarkDialogTitle.textContent
      );

      Assert.equal(
        namepicker.value,
        titleAfterFirstUpdate,
        "Name field is correct before update."
      );

      let promiseTitleChange = PlacesTestUtils.waitForNotification(
        "bookmark-title-changed",
        events => events.some(e => e.title === "Toolbar title")
      );

      // Update the bookmark's title.
      fillBookmarkTextField(
        "editBMPanel_namePicker",
        "Toolbar title",
        dialogWin,
        false
      );

      // Confirm and close the dialog.
      EventUtils.synthesizeKey("VK_RETURN", {}, dialogWin);
      await promiseTitleChange;

      let updatedBm = await PlacesUtils.bookmarks.fetch(toolbarBookmark.guid);
      Assert.equal(
        updatedBm.title,
        "Toolbar title",
        "Should have updated the bookmark title in the database"
      );
    }
  );
});

add_task(async function test_change_title_from_Sidebar() {
  let bookmarks = [];
  await PlacesUtils.bookmarks.fetch({ url: TEST_URL }, bm =>
    bookmarks.push(bm)
  );

  await withSidebarTree("bookmarks", async function (tree) {
    tree.selectItems([bookmarks[0].guid]);

    await withBookmarksDialog(
      false,
      function openPropertiesDialog() {
        tree.controller.doCommand("placesCmd_show:info");
      },
      async function test(dialogWin) {
        let namepicker = dialogWin.document.getElementById(
          "editBMPanel_namePicker"
        );
        Assert.equal(
          namepicker.value,
          "Toolbar title",
          "Name field is correct before update."
        );

        let promiseTitleChange = PlacesTestUtils.waitForNotification(
          "bookmark-title-changed",
          events => events.some(e => e.title === "Sidebar Title")
        );

        // Update the bookmark's title.
        fillBookmarkTextField(
          "editBMPanel_namePicker",
          "Sidebar Title",
          dialogWin,
          false
        );

        // Confirm and close the dialog.
        EventUtils.synthesizeKey("VK_RETURN", {}, dialogWin);
        await promiseTitleChange;

        // Get updated bookmarks, check the new title.
        bookmarks = [];
        await PlacesUtils.bookmarks.fetch({ url: TEST_URL }, bm =>
          bookmarks.push(bm)
        );
        Assert.equal(
          bookmarks[0].title,
          "Sidebar Title",
          "Should have updated the bookmark title in the database"
        );
        Assert.equal(bookmarks.length, 1, "One bookmark should exist");
      }
    );
  });
});

add_task(async function test_change_title_from_Library() {
  info("Open library and select the bookmark.");
  const library = await promiseLibrary("BookmarksToolbar");
  registerCleanupFunction(async function () {
    await promiseLibraryClosed(library);
  });
  library.ContentTree.view.selectNode(
    library.ContentTree.view.view.nodeForTreeIndex(0)
  );
  const newTitle = "Library";
  const promiseTitleChange = PlacesTestUtils.waitForNotification(
    "bookmark-title-changed",
    events => events.some(e => e.title === newTitle)
  );
  info("Update the bookmark's title.");
  fillBookmarkTextField("editBMPanel_namePicker", newTitle, library);
  await promiseTitleChange;
  info("The bookmark's title was updated.");
  await promiseLibraryClosed(library);
});
