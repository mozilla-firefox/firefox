ChromeUtils.defineESModuleGetters(this, {
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "gFluentStrings", function () {
  return new Localization(["branding/brand.ftl", "browser/browser.ftl"], true);
});

/**
 * Returns a handle to a Library window.
 * If one is opens returns itm otherwise it opens a new one.
 *
 * @param {object} aLeftPaneRoot
 *        Hierarchy to open and select in the left pane.
 * @param {Window} [win]
 *        The window to use to open the library.
 * @returns {Promise}
 *          Resolves to the handle to the library window.
 */
async function promiseLibrary(aLeftPaneRoot, win = window) {
  let library = Services.wm.getMostRecentWindow("Places:Organizer");
  if (!library || library.closed) {
    library = win.openDialog(
      "chrome://browser/content/places/places.xhtml",
      "",
      "chrome,toolbar=yes,dialog=no,resizable",
      aLeftPaneRoot
    );
  } else if (aLeftPaneRoot) {
    library.PlacesOrganizer.selectLeftPaneContainerByHierarchy(aLeftPaneRoot);
  }
  await SimpleTest.promiseFocus(library);
  checkLibraryPaneVisibility(library, aLeftPaneRoot);
  return library;
}

async function promiseLibraryClosed(organizer) {
  if (organizer.closed) {
    return;
  }
  // Wait for the Organizer window to actually be closed.
  let promiseClosed = BrowserTestUtils.domWindowClosed(organizer);
  organizer.close();
  await promiseClosed;
}

function checkLibraryPaneVisibility(library, selectedPane) {
  // Make sure right view is shown
  if (selectedPane == "Downloads") {
    Assert.ok(
      library.ContentTree.view.hidden,
      "Bookmark/History tree is hidden"
    );
    Assert.ok(
      !library.document.getElementById("downloadsListBox").hidden,
      "Downloads are shown"
    );
  } else {
    Assert.ok(
      !library.ContentTree.view.hidden,
      "Bookmark/History tree is shown"
    );
    Assert.ok(
      library.document.getElementById("downloadsListBox").hidden,
      "Downloads are hidden"
    );
  }

  // Check currentView getter
  Assert.ok(!library.ContentArea.currentView.hidden, "Current view is shown");
}

/**
 * Waits for a clipboard operation to complete, looking for the expected type.
 *
 * @see waitForClipboard
 *
 * @param {Function} aPopulateClipboardFn
 *        Function to populate the clipboard.
 * @param {string} aFlavor
 *        Data flavor to expect.
 * @returns {Promise}
 *          A promise that is resolved with the data.
 */
function promiseClipboard(aPopulateClipboardFn, aFlavor) {
  return new Promise((resolve, reject) => {
    waitForClipboard(
      data => !!data,
      aPopulateClipboardFn,
      resolve,
      reject,
      aFlavor
    );
  });
}

async function synthesizeClickOnSelectedTreeCell(aTree, aOptions) {
  if (aTree.view.selection.count < 1) {
    throw new Error("The test node should be successfully selected");
  }
  await TestUtils.waitForCondition(
    () => aTree.getBoundingClientRect().width > 0,
    "Tree should have non-zero width before clicking"
  );
  // Get selection rowID.
  let min = {},
    max = {};
  aTree.view.selection.getRangeAt(0, min, max);
  let rowID = min.value;
  aTree.ensureRowIsVisible(rowID);
  // Calculate the click coordinates. getCoordsForCellItem returns the width the
  // text would need rather than the space it actually gets, so for a cropped
  // cell the rect can extend past the tree body (bug 1708159). Clamp it to the
  // body's content box, which is what the rows are laid out in, or the midpoint
  // we click lands outside the tree and hits an unrelated element.
  let rect = aTree.getCoordsForCellItem(rowID, aTree.columns[0], "text");
  let minX = aTree.body.clientLeft;
  let maxX = minX + aTree.body.clientWidth;
  let x = (Math.max(rect.x, minX) + Math.min(rect.x + rect.width, maxX)) / 2;
  let y = rect.y + rect.height / 2;
  if (aTree.id == "bookmarks-view" || aTree.id == "historyTree") {
    // We are purposefully keeping the main <tree> element unlabeled, because in
    // this specific case, the on-screen label for either "Bookmarks" or
    // "History" sidebar is positioned closely to the tree, visually and in DOM.
    // We want to avoid making a screen reader user to listen to a redundant
    // announcement, therefore no accessible name is provided to the container
    // and we account for this in a11y-checks:
    AccessibilityUtils.setEnv({
      labelRule: false,
    });
  }
  // Simulate the click.
  EventUtils.synthesizeMouse(
    aTree.body,
    x,
    y,
    aOptions || {},
    aTree.documentGlobal
  );
  AccessibilityUtils.resetEnv();
}

/**
 * Makes the specified toolbar visible or invisible and returns a Promise object
 * that is resolved when the toolbar has completed any animations associated
 * with hiding or showing the toolbar.
 *
 * Note that this code assumes that changes to a toolbar's visibility trigger
 * a transition on the max-height property of the toolbar element.
 * Changes to this styling could cause the returned Promise object to be
 * resolved too early or not at all.
 *
 * @param {object} aToolbar
 *        The toolbar to update.
 * @param {boolean} aVisible
 *        True to make the toolbar visible, false to make it hidden.
 *
 * @returns {Promise} Any animation associated with updating the toolbar's
 *                    visibility has finished.
 */
function promiseSetToolbarVisibility(aToolbar, aVisible) {
  if (isToolbarVisible(aToolbar) != aVisible) {
    let visibilityChanged = TestUtils.waitForCondition(
      () => isToolbarVisible(aToolbar) == aVisible
    );
    setToolbarVisibility(aToolbar, aVisible, undefined, false);
    return visibilityChanged;
  }
  return Promise.resolve();
}

/**
 * Helper function to determine if the given toolbar is in the visible
 * state according to its autohide/collapsed attribute.
 *
 * @param {object} aToolbar The toolbar to query.
 *
 * @returns {boolean} True if the relevant attribute on |aToolbar| indicates it is
 *          visible, false otherwise.
 */
function isToolbarVisible(aToolbar) {
  let hidingAttribute =
    aToolbar.getAttribute("type") == "menubar" ? "autohide" : "collapsed";
  return !aToolbar.hasAttribute(hidingAttribute);
}

/**
 * Executes a task after opening the bookmarks dialog, then cancels the dialog.
 *
 * @param {boolean} autoCancel
 *        whether to automatically cancel the dialog at the end of the task
 * @param {Function} openFn
 *        generator function causing the dialog to open
 * @param {Function} taskFn
 *        the task to execute once the dialog is open
 * @param {Function} closeFn
 *        A function to be used to wait for pending work when the dialog is
 *        closing. It is passed the dialog window handle and should return a promise.
 * @returns {string} guid
 *          Bookmark guid
 */
var withBookmarksDialog = async function (autoCancel, openFn, taskFn, closeFn) {
  let dialogUrl = "chrome://browser/content/places/bookmarkProperties.xhtml";
  let closed = false;
  // We can't show the in-window prompt for windows which don't have
  // gDialogBox, like the library (Places:Organizer) window.
  let hasDialogBox = !!Services.wm.getMostRecentWindow("").gDialogBox;
  let dialogPromise;
  if (hasDialogBox) {
    dialogPromise = BrowserTestUtils.promiseAlertDialogOpen(null, dialogUrl, {
      isSubDialog: true,
    });
  } else {
    dialogPromise = BrowserTestUtils.domWindowOpenedAndLoaded(null, win => {
      return win.document.documentURI.startsWith(dialogUrl);
    }).then(win => {
      ok(
        win.location.href.startsWith(dialogUrl),
        "The bookmark properties dialog is open: " + win.location.href
      );
      // This is needed for the overlay.
      return SimpleTest.promiseFocus(win).then(() => win);
    });
  }
  let dialogClosePromise = dialogPromise.then(win => {
    if (!hasDialogBox) {
      return BrowserTestUtils.domWindowClosed(win);
    }
    let container = win.top.document.getElementById("window-modal-dialog");
    return BrowserTestUtils.waitForEvent(container, "close").then(() => {
      return BrowserTestUtils.waitForMutationCondition(
        container,
        { childList: true, attributes: true },
        () => !container.hasChildNodes() && !container.open
      );
    });
  });
  dialogClosePromise.then(() => {
    closed = true;
  });

  info("withBookmarksDialog: opening the dialog");
  // The dialog might be modal and could block our events loop, so executeSoon.
  executeSoon(openFn);

  info("withBookmarksDialog: waiting for the dialog");
  let dialogWin = await dialogPromise;

  // Ensure overlay is loaded
  info("waiting for the overlay to be loaded");
  await dialogWin.document.mozSubdialogReady;

  // Check the first input is focused.
  let doc = dialogWin.document;
  let elt = doc.querySelector('input:not([hidden="true"])');
  ok(elt, "There should be an input to focus.");

  if (elt) {
    info("waiting for focus on the first textfield");
    await TestUtils.waitForCondition(
      () => doc.activeElement == elt,
      "The first non collapsed input should have been focused"
    );
  }

  info("withBookmarksDialog: executing the task");

  let closePromise = () => Promise.resolve();
  if (closeFn) {
    closePromise = closeFn(dialogWin);
  }
  let guid;
  try {
    await taskFn(dialogWin);
  } finally {
    if (!closed && autoCancel) {
      info("withBookmarksDialog: canceling the dialog");
      doc.getElementById("bookmarkpropertiesdialog").cancelDialog();
      await closePromise;
    }
    guid = await PlacesUIUtils.lastBookmarkDialogDeferred.promise;
    // Give the dialog a little time to close itself.
    await dialogClosePromise;
  }
  return guid;
};

/**
 * Opens the contextual menu on the element pointed by the given selector.
 *
 * @param {object} browser
 *        The associated browser element.
 * @param {object} selector
 *        Valid selector syntax
 * @returns {Promise}
 *         Returns a Promise that resolves once the context menu has been
 *         opened.
 */
var openContextMenuForContentSelector = async function (browser, selector) {
  info("wait for the context menu");
  let contextPromise = BrowserTestUtils.waitForEvent(
    document.getElementById("contentAreaContextMenu"),
    "popupshown"
  );
  await SpecialPowers.spawn(browser, [{ selector }], async function (args) {
    let doc = content.document;
    let elt = doc.querySelector(args.selector);
    dump(`openContextMenuForContentSelector: found ${elt}\n`);

    /* Open context menu so chrome can access the element */
    EventUtils.synthesizeMouseAtCenter(
      elt,
      { type: "contextmenu", button: 2, clickCount: 1, buttons: 0 },
      content
    );
  });
  await contextPromise;
};

/**
 * Fills a bookmarks dialog text field ensuring to cause expected edit events.
 *
 * @param {string} id
 *        id of the text field
 * @param {string} text
 *        text to fill in
 * @param {object} win
 *        dialog window
 * @param {boolean} [blur]
 *        whether to blur at the end.
 */
function fillBookmarkTextField(id, text, win, blur = true) {
  let elt = win.document.getElementById(id);
  elt.focus();
  elt.select();
  if (!text) {
    EventUtils.synthesizeKey("VK_DELETE", {}, win);
  } else {
    for (let c of text.split("")) {
      EventUtils.synthesizeKey(c, {}, win);
    }
  }
  if (blur) {
    elt.blur();
  }
}

/**
 * Executes a task after opening the bookmarks or history sidebar. Takes care
 * of closing the sidebar once done.
 *
 * @param {string} type
 *        either "bookmarks" or "history".
 * @param {Function} taskFn
 *        The task to execute once the sidebar is ready. Will get the Places
 *        tree view as input.
 * @param {Window} [win]
 *        The window to open the sidebar in, else uses the default window.
 */
var withSidebarTree = async function (type, taskFn, win = window) {
  let sidebar = win.document.getElementById("sidebar");
  info("withSidebarTree: waiting for the sidebar to show");
  let sidebarId =
    type == "bookmarks" ? "viewBookmarksSidebar" : "viewHistorySidebar";
  await win.SidebarController.show(sidebarId);

  let treeId = type == "bookmarks" ? "bookmarks-view" : "historyTree";
  let tree = sidebar.contentDocument.getElementById(treeId);

  info("withSidebarTree: executing the task");
  try {
    await taskFn(tree);
  } finally {
    win.SidebarController.hide();
  }
};

/**
 * Executes a task after opening the Library on a given root. Takes care
 * of closing the library once done.
 *
 * @param {string} hierarchy
 *        The left pane hierarchy to open.
 * @param {Function} taskFn
 *        The task to execute once the Library is ready.
 *        Will get { left, right } trees as argument.
 * @param {Window} [win]
 *        The window to use to open the library.
 */
var withLibraryWindow = async function (hierarchy, taskFn, win = window) {
  let library = await promiseLibrary(hierarchy, win);
  let left = library.document.getElementById("placesList");
  let right = library.document.getElementById("placeContent");
  info("withLibrary: executing the task");
  try {
    await taskFn({ left, right });
  } finally {
    await promiseLibraryClosed(library);
  }
};

function promisePlacesInitComplete() {
  const gBrowserGlue = Cc["@mozilla.org/browser/browserglue;1"].getService(
    Ci.nsIObserver
  );

  let placesInitCompleteObserved = TestUtils.topicObserved(
    "places-browser-init-complete"
  );

  gBrowserGlue.observe(
    null,
    "browser-glue-test",
    "places-browser-init-complete"
  );

  return placesInitCompleteObserved;
}

// Function copied from browser/base/content/test/browser-general/head.js.
function promisePopupShown(popup) {
  return new Promise(resolve => {
    if (popup.state == "open") {
      resolve();
    } else {
      let onPopupShown = () => {
        popup.removeEventListener("popupshown", onPopupShown);
        resolve();
      };
      popup.addEventListener("popupshown", onPopupShown);
    }
  });
}

// Function copied from browser/base/content/test/browser-general/head.js.
function promisePopupHidden(popup) {
  return new Promise(resolve => {
    let onPopupHidden = () => {
      popup.removeEventListener("popuphidden", onPopupHidden);
      resolve();
    };
    popup.addEventListener("popuphidden", onPopupHidden);
  });
}

/**
 * Boilerplate code to ensure the bookmarks toolbar is visible and contains
 * at least one bookmark.
 */
async function ensureBookmarksToolbarIsVisibleAndPopulated() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.toolbars.bookmarks.visibility", "always"]],
  });

  // Necessary to avoid intermittent failures in verify-fission where default
  // bookmarks may or may not have been imported yet.
  await promisePlacesInitComplete();
  await PlacesUtils.bookmarks.eraseEverything();

  // Avoid the empty toolbar placeholder shifting stuff around.
  let bm = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "initial",
    url: "about:robots",
  });

  let toolbar = document.getElementById("PersonalToolbar");
  let wasCollapsed = toolbar.collapsed;
  if (wasCollapsed) {
    await promiseSetToolbarVisibility(toolbar, true);
    await BrowserTestUtils.waitForEvent(
      toolbar,
      "BookmarksToolbarVisibilityUpdated"
    );
  }

  registerCleanupFunction(async () => {
    if (wasCollapsed) {
      await promiseSetToolbarVisibility(toolbar, false);
    }
    try {
      await PlacesUtils.bookmarks.remove(bm);
    } catch (ex) {
      // The bookmark may have been removed already.
    }
  });

  await waitForBookmarksToolbarElements(1);
}

/**
 * Ensure N bookmarks are visible on the Bookmarks Toolbar.
 *
 * @param {integer} expectedCount The number of bookmarks to wait for.
 * @returns {Promise} resolved when the condition is satisfied.
 */
function waitForBookmarksToolbarElements(expectedCount) {
  let container = document.getElementById("PlacesToolbarItems");
  if (container.childElementCount == expectedCount) {
    return Promise.resolve();
  }
  return new Promise(resolve => {
    info("Waiting for bookmarks");
    let mut = new MutationObserver(() => {
      if (container.childElementCount == expectedCount) {
        resolve();
        mut.disconnect();
      }
    });
    mut.observe(container, { childList: true });
  });
}

// Identify a bookmark node in the Bookmarks Toolbar by its guid.
function getToolbarNodeForItemGuid(itemGuid, win = window) {
  let children = win.document.getElementById("PlacesToolbarItems").childNodes;
  for (let child of children) {
    if (itemGuid === child._placesNode.bookmarkGuid) {
      return child;
    }
  }
  return null;
}

// Open the bookmarks Star UI by clicking the star button on the address bar.
async function clickBookmarkStar(win = window) {
  let shownPromise = promisePopupShown(
    win.document.getElementById("editBookmarkPanel")
  );
  win.BookmarkingUI.star.click();
  await shownPromise;

  // Additionally await for the async init to complete.
  let menuList = win.document.getElementById("editBMPanel_folderMenuList");
  await BrowserTestUtils.waitForMutationCondition(
    menuList,
    { attributes: true },
    () => !!menuList.getAttribute("selectedGuid"),
    { msg: "Should select the menu folder item" }
  );
}

// Close the bookmarks Star UI by clicking the "Done" button.
async function hideBookmarksPanel(win = window) {
  let hiddenPromise = promisePopupHidden(
    win.document.getElementById("editBookmarkPanel")
  );
  // Confirm and close the dialog.
  win.document.getElementById("editBookmarkPanelDoneButton").click();
  await hiddenPromise;
}

// Create a temporary folder, set it as the default folder,
// then remove the folder. This is used to ensure that the
// default folder gets reset properly.
async function createAndRemoveDefaultFolder() {
  let tempFolder = await PlacesUtils.bookmarks.insertTree({
    guid: PlacesUtils.bookmarks.unfiledGuid,
    children: [
      {
        title: "temp folder",
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
      },
    ],
  });

  await SpecialPowers.pushPrefEnv({
    set: [["browser.bookmarks.defaultLocation", tempFolder[0].guid]],
  });

  let defaultGUID = await PlacesUIUtils.defaultParentGuid;
  is(defaultGUID, tempFolder[0].guid, "check default guid");

  await PlacesUtils.bookmarks.remove(tempFolder);
}

async function showLibraryColumn(library, columnName) {
  await SimpleTest.promiseFocus(library);

  const viewMenu = library.document.getElementById("viewMenu");
  info("await viewMenuPopup");
  await new Promise(resolve => {
    library.document
      .getElementById("viewMenuPopup")
      .addEventListener("popupshown", resolve, {
        once: true,
      });
    viewMenu.click();
  });

  const viewColumns = library.document.getElementById("viewColumns");
  info("await viewColumnsPopup");
  await new Promise(resolve => {
    viewColumns
      .querySelector("menupopup")
      .addEventListener("popupshown", () => resolve(), {
        once: true,
      });
    viewColumns.click();
  });

  library.document.getElementById(`menucol_${columnName}`).click();
}

// Given a moz-input-search element, fill it with the query `query`.
function setSearch(searchBox, query) {
  return new Promise(resolve => {
    searchBox.addEventListener("MozInputSearch:search", resolve, {
      once: true,
    });
    searchBox.select();
    if (query) {
      EventUtils.sendString(query, searchBox.documentGlobal);
    } else {
      searchBox.clear();
    }
  });
}

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref("browser.bookmarks.defaultLocation");
  await PlacesTransactions.clearTransactionsHistory(true, true);
});
