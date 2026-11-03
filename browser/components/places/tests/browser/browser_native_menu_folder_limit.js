/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

const FOLDER_LIMIT_PREF = "browser.places.nativeMenu.maxFolderResults";
const DEFAULT_FOLDER_LIMIT = 500;
const FOLDER_RESULT_COUNT = DEFAULT_FOLDER_LIMIT + 5;
const MORE_SELECTOR = '[data-l10n-id="menu-places-show-more-in-sidebar"]';
let folder;
let bookmarks;

add_setup(async function () {
  registerCleanupFunction(async () => {
    SidebarController.hide();
    await PlacesUtils.bookmarks.eraseEverything();
  });
  await PlacesUtils.bookmarks.eraseEverything();
  [folder, ...bookmarks] = await PlacesUtils.bookmarks.insertTree({
    guid: PlacesUtils.bookmarks.menuGuid,
    children: [
      {
        title: "Native folder limit",
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        children: Array.from({ length: FOLDER_RESULT_COUNT }, (_, i) => ({
          title: `Bookmark ${i}`,
          url: `https://example.com/native-folder-limit/${i}`,
        })),
      },
    ],
  });
});

function createFolderMenu(place, isNative = true, isOpen = false) {
  let menu = document.createXULElement("menu");
  let popup = document.createXULElement("menupopup");
  menu.appendChild(popup);
  if (isOpen) {
    menu.setAttribute("open", "true");
  }
  document
    .getElementById(isNative ? "bookmarksMenuPopup" : "mainPopupSet")
    .appendChild(menu);
  let view = new PlacesMenu({ target: popup, originalTarget: popup }, place);
  return { menu, popup, view };
}

function showFolder(popup) {
  popup.dispatchEvent(new MouseEvent("popuphidden", { bubbles: true }));
  popup.dispatchEvent(new MouseEvent("popupshowing", { bubbles: true }));
}

function folderItems(popup) {
  return Array.from(popup.children).filter(child => child._placesNode);
}

add_task(async function test_default_folder_limit() {
  Assert.equal(
    Services.prefs.getIntPref(FOLDER_LIMIT_PREF),
    DEFAULT_FOLDER_LIMIT
  );
  let { menu, popup, view } = createFolderMenu(`place:parent=${folder.guid}`);
  try {
    Assert.equal(
      folderItems(popup).length,
      DEFAULT_FOLDER_LIMIT,
      "The native folder menu shows at most 500 entries"
    );
    let more = popup.querySelector(MORE_SELECTOR);
    Assert.ok(more, "An overflowing folder offers the sidebar");
    let shown = BrowserTestUtils.waitForEvent(window, "SidebarShown");
    more.doCommand();
    await shown;
    Assert.equal(
      SidebarController.currentID,
      "viewBookmarksSidebar",
      "A folder opens the bookmarks sidebar"
    );
  } finally {
    SidebarController.hide();
    view.uninit();
    menu.remove();
  }
});

add_task(async function test_initially_open_folder() {
  let observer = new MutationObserver(() => {});
  observer.observe(document.getElementById("bookmarksMenuPopup"), {
    childList: true,
    subtree: true,
  });
  let { menu, popup, view } = createFolderMenu(
    `place:parent=${folder.guid}`,
    true,
    true
  );
  try {
    Assert.equal(folderItems(popup).length, DEFAULT_FOLDER_LIMIT);
    for (let record of observer.takeRecords()) {
      let addedItems = Array.from(record.addedNodes).filter(
        node => node._placesNode
      );
      Assert.lessOrEqual(
        addedItems.length,
        DEFAULT_FOLDER_LIMIT,
        "An initially open menu never inserts the entire uncapped folder"
      );
    }
  } finally {
    observer.disconnect();
    view.uninit();
    menu.removeAttribute("open");
    menu.remove();
  }
});

add_task(async function test_nested_folder_and_shortcut() {
  let shortcut = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.menuGuid,
    title: "Folder shortcut",
    url: `place:parent=${folder.guid}`,
  });
  let { menu, popup, view } = createFolderMenu(
    `place:parent=${PlacesUtils.bookmarks.menuGuid}`
  );
  try {
    for (let guid of [folder.guid, shortcut.guid]) {
      let childPopup = folderItems(popup).find(
        child => child._placesNode.bookmarkGuid == guid
      ).menupopup;
      showFolder(childPopup);
      Assert.equal(
        folderItems(childPopup).length,
        DEFAULT_FOLDER_LIMIT,
        "Ordinary folders and shortcuts have the same cap"
      );
      Assert.ok(
        childPopup.querySelector(MORE_SELECTOR),
        "The overflowing submenu offers the sidebar"
      );
      let shown = BrowserTestUtils.waitForEvent(window, "SidebarShown");
      childPopup.querySelector(MORE_SELECTOR).doCommand();
      await shown;
      Assert.equal(SidebarController.currentID, "viewBookmarksSidebar");
      SidebarController.hide();
    }
  } finally {
    view.uninit();
    menu.remove();
    await PlacesUtils.bookmarks.remove(shortcut.guid);
  }
});

add_task(async function test_pref_changes_and_nonnative_menu() {
  let { menu, popup, view } = createFolderMenu(`place:parent=${folder.guid}`);
  try {
    for (let limit of [3, FOLDER_RESULT_COUNT, 0, DEFAULT_FOLDER_LIMIT]) {
      await SpecialPowers.pushPrefEnv({ set: [[FOLDER_LIMIT_PREF, limit]] });
      try {
        showFolder(popup);
        Assert.equal(
          folderItems(popup).length,
          limit || FOLDER_RESULT_COUNT,
          "Reopening a cached folder uses the current limit"
        );
        Assert.equal(
          !!popup.querySelector(MORE_SELECTOR),
          limit > 0 && limit < FOLDER_RESULT_COUNT,
          "Only overflowing folders offer the sidebar"
        );
      } finally {
        await SpecialPowers.popPrefEnv();
      }
    }
  } finally {
    view.uninit();
    menu.remove();
  }
  ({ menu, popup, view } = createFolderMenu(
    `place:parent=${folder.guid}`,
    false
  ));
  try {
    Assert.equal(
      folderItems(popup).length,
      FOLDER_RESULT_COUNT,
      "Nonnative menus stay unlimited"
    );
    Assert.ok(
      !popup.querySelector(MORE_SELECTOR),
      "The nonnative menu has no overflow command"
    );
  } finally {
    view.uninit();
    menu.remove();
  }
});

add_task(async function test_folder_changes() {
  await SpecialPowers.pushPrefEnv({ set: [[FOLDER_LIMIT_PREF, 3]] });
  let { menu, popup, view } = createFolderMenu(`place:parent=${folder.guid}`);
  let added;
  try {
    let originalItems = folderItems(popup);
    added = await PlacesUtils.bookmarks.insert({
      parentGuid: folder.guid,
      index: 0,
      title: "Inserted first",
      url: "https://example.com/native-folder-limit/inserted",
    });
    Assert.equal(
      folderItems(popup).length,
      3,
      "Insertions stay within the cap without reopening the menu"
    );
    Assert.equal(
      folderItems(popup)[1],
      originalItems[0],
      "Inserting keeps the existing first bookmark element"
    );
    Assert.equal(
      folderItems(popup)[2],
      originalItems[1],
      "Inserting keeps the existing second bookmark element"
    );
    Assert.ok(
      !originalItems[2].isConnected,
      "The entry pushed beyond the cap is removed"
    );
    Assert.equal(folderItems(popup)[0].label, "Inserted first");
    await PlacesUtils.bookmarks.update({
      guid: bookmarks[FOLDER_RESULT_COUNT - 1].guid,
      title: "Renamed hidden bookmark",
    });
    await PlacesUtils.bookmarks.update({
      guid: bookmarks[FOLDER_RESULT_COUNT - 1].guid,
      index: 0,
    });
    Assert.equal(
      folderItems(popup)[0].label,
      "Renamed hidden bookmark",
      "A hidden bookmark can be renamed and moved into the visible range"
    );
    Assert.equal(folderItems(popup).length, 3);
    await PlacesUtils.bookmarks.update({
      guid: bookmarks[FOLDER_RESULT_COUNT - 1].guid,
      title: "Renamed visible bookmark",
    });
    Assert.equal(
      folderItems(popup)[0].label,
      "Renamed visible bookmark",
      "Visible entries still receive title updates"
    );
    await PlacesUtils.bookmarks.update({
      guid: bookmarks[FOLDER_RESULT_COUNT - 1].guid,
      index: FOLDER_RESULT_COUNT - 1,
    });
    Assert.equal(
      folderItems(popup)[0].label,
      "Inserted first",
      "Moving a visible entry beyond the cap fills its place"
    );
    let selectedItem = folderItems(popup)[0];
    let remainingItem = folderItems(popup)[1];
    let mutations = [];
    let observer = new MutationObserver(records => mutations.push(...records));
    observer.observe(popup, { childList: true });
    try {
      await PlacesUtils.bookmarks.remove(added.guid);
      added = null;
      mutations.push(...observer.takeRecords());
    } finally {
      observer.disconnect();
    }
    let removedItems = mutations
      .flatMap(record => Array.from(record.removedNodes))
      .filter(node => node._placesNode);
    Assert.deepEqual(
      removedItems,
      [selectedItem],
      "Deleting a bookmark does not remove and reinsert the other items"
    );
    Assert.equal(
      folderItems(popup)[0],
      remainingItem,
      "Deleting the first bookmark keeps the remaining element in place"
    );
    Assert.equal(
      folderItems(popup).length,
      3,
      "Removing a visible entry fills its place"
    );
    await PlacesUtils.bookmarks.remove(bookmarks[FOLDER_RESULT_COUNT - 2].guid);
    showFolder(popup);
    Assert.equal(
      folderItems(popup).length,
      3,
      "Hidden removals preserve the visible cap"
    );
    Assert.ok(
      popup.querySelector(MORE_SELECTOR),
      "The overflowing folder offers the sidebar"
    );
    // Move the visible boundary over a separator and a folder.
    await PlacesUtils.bookmarks.insert({
      parentGuid: folder.guid,
      index: 0,
      type: PlacesUtils.bookmarks.TYPE_SEPARATOR,
    });
    let childFolder = await PlacesUtils.bookmarks.insert({
      parentGuid: folder.guid,
      index: 0,
      title: "Child folder",
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
    });
    showFolder(popup);
    Assert.equal(
      folderItems(popup).length,
      3,
      "Folders and separators count toward the cap"
    );
    Assert.equal(folderItems(popup)[0].localName, "menu");
    Assert.equal(folderItems(popup)[1].localName, "menuseparator");
    await PlacesUtils.bookmarks.remove(childFolder.guid);
    showFolder(popup);
    Assert.equal(folderItems(popup).length, 3);
  } finally {
    if (added) {
      await PlacesUtils.bookmarks.remove(added.guid);
    }
    view.uninit();
    menu.remove();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_folder_becomes_empty() {
  await SpecialPowers.pushPrefEnv({ set: [[FOLDER_LIMIT_PREF, 1]] });
  let entries = await PlacesUtils.bookmarks.insertTree({
    guid: PlacesUtils.bookmarks.menuGuid,
    children: [
      {
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        title: "Folder to empty",
        children: [
          { url: "https://example.com/empty/1" },
          { url: "https://example.com/empty/2" },
        ],
      },
    ],
  });
  let { menu, popup, view } = createFolderMenu(
    `place:parent=${entries[0].guid}`
  );
  try {
    Assert.equal(folderItems(popup).length, 1);
    Assert.ok(
      popup.querySelector(MORE_SELECTOR),
      "The overflowing folder offers the sidebar"
    );
    await PlacesUtils.bookmarks.remove(
      entries.slice(1).map(({ guid }) => ({ guid }))
    );
    showFolder(popup);
    Assert.equal(folderItems(popup).length, 0);
    Assert.ok(
      !popup.querySelector(MORE_SELECTOR),
      "An empty folder has no overflow command"
    );
    Assert.ok(
      popup.querySelector('[data-l10n-id="places-empty-bookmarks-folder"]'),
      "An empty folder shows its empty-folder label"
    );
  } finally {
    view.uninit();
    menu.remove();
    await PlacesUtils.bookmarks.remove(entries[0].guid);
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_folder_sorting_invalidates_menu() {
  await SpecialPowers.pushPrefEnv({ set: [[FOLDER_LIMIT_PREF, 2]] });
  let entries = await PlacesUtils.bookmarks.insertTree({
    guid: PlacesUtils.bookmarks.menuGuid,
    children: [
      {
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        title: "Folder to sort",
        children: ["C", "B", "A"].map(title => ({
          title,
          url: `https://example.com/sorting/${title}`,
        })),
      },
    ],
  });
  let { menu, popup, view } = createFolderMenu(
    `place:parent=${entries[0].guid}`
  );
  try {
    Assert.deepEqual(
      folderItems(popup).map(item => item.label),
      ["C", "B"]
    );
    view.result.sortingMode =
      Ci.nsINavHistoryQueryOptions.SORT_BY_TITLE_ASCENDING;
    showFolder(popup);
    Assert.deepEqual(
      folderItems(popup).map(item => item.label),
      ["A", "B"],
      "Sorting rebuilds the capped folder in the new order"
    );
    Assert.ok(
      popup.querySelector(MORE_SELECTOR),
      "The sorted folder still offers the sidebar"
    );
  } finally {
    view.uninit();
    menu.remove();
    await PlacesUtils.bookmarks.remove(entries[0].guid);
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_changes_in_detached_folder() {
  await SpecialPowers.pushPrefEnv({ set: [[FOLDER_LIMIT_PREF, 1]] });
  let entries = await PlacesUtils.bookmarks.insertTree({
    guid: PlacesUtils.bookmarks.menuGuid,
    children: [
      {
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        title: "Parent folder",
        children: [
          {
            type: PlacesUtils.bookmarks.TYPE_FOLDER,
            title: "Nested folder",
            children: ["First", "Second"].map(title => ({
              title,
              url: `https://example.com/detached/${title}`,
            })),
          },
        ],
      },
    ],
  });
  let parent = entries[0];
  let nested = entries.find(entry => entry.title == "Nested folder");
  let { menu, popup, view } = createFolderMenu(`place:parent=${parent.guid}`);
  try {
    let nestedPopup = folderItems(popup)[0].menupopup;
    showFolder(nestedPopup);
    Assert.equal(folderItems(nestedPopup).length, 1);
    nestedPopup.dispatchEvent(new MouseEvent("popuphidden", { bubbles: true }));
    let inserted = await PlacesUtils.bookmarks.insert({
      parentGuid: parent.guid,
      index: 0,
      title: "Push the nested folder past the limit",
      url: "https://example.com/detached/parent",
    });
    showFolder(popup);
    Assert.ok(
      !nestedPopup.isConnected,
      "The previously opened folder is outside the visible range"
    );
    await SpecialPowers.pushPrefEnv({ set: [[FOLDER_LIMIT_PREF, 0]] });
    try {
      await PlacesUtils.bookmarks.insert({
        parentGuid: nested.guid,
        index: 0,
        title: "Inserted in detached folder",
        url: "https://example.com/detached/inserted",
      });
      await PlacesUtils.bookmarks.update({
        guid: entries.find(entry => entry.title == "Second").guid,
        title: "Renamed in detached folder",
      });
      await PlacesUtils.bookmarks.remove(inserted.guid);
      showFolder(popup);
      let restoredPopup = folderItems(popup)[0].menupopup;
      showFolder(restoredPopup);
      Assert.deepEqual(
        folderItems(restoredPopup).map(item => item.label),
        ["Inserted in detached folder", "First", "Renamed in detached folder"],
        "The restored folder includes changes made while its old popup was detached"
      );
      Assert.ok(
        !restoredPopup.querySelector(MORE_SELECTOR),
        "Disabling the limit restores the full folder"
      );
    } finally {
      await SpecialPowers.popPrefEnv();
    }
  } finally {
    view.uninit();
    menu.remove();
    await PlacesUtils.bookmarks.remove(parent.guid);
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_visible_move_preserves_siblings() {
  await SpecialPowers.pushPrefEnv({ set: [[FOLDER_LIMIT_PREF, 3]] });
  let entries = await PlacesUtils.bookmarks.insertTree({
    guid: PlacesUtils.bookmarks.menuGuid,
    children: [
      {
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        title: "Folder with a visible move",
        children: ["A", "B", "C", "D"].map(title => ({
          title,
          url: `https://example.com/visible-move/${title}`,
        })),
      },
    ],
  });
  let { menu, popup, view } = createFolderMenu(
    `place:parent=${entries[0].guid}`
  );
  try {
    let [first, second, third] = folderItems(popup);
    let mutations = [];
    let observer = new MutationObserver(records => mutations.push(...records));
    observer.observe(popup, { childList: true });
    try {
      await PlacesUtils.bookmarks.update({
        guid: entries.find(entry => entry.title == "A").guid,
        index: 2,
      });
      mutations.push(...observer.takeRecords());
    } finally {
      observer.disconnect();
    }
    Assert.deepEqual(
      folderItems(popup),
      [second, third, first],
      "A visible move updates the menu without reopening it"
    );
    let removedItems = mutations
      .flatMap(record => Array.from(record.removedNodes))
      .filter(node => node._placesNode);
    Assert.deepEqual(
      removedItems,
      [first],
      "Moving the first bookmark to the end leaves its siblings untouched"
    );
    let movedGuid = entries.find(entry => entry.title == "A").guid;
    await PlacesUtils.bookmarks.update({ guid: movedGuid, index: 3 });
    Assert.deepEqual(
      folderItems(popup).map(item => item.label),
      ["B", "C", "D"],
      "Moving to the first hidden index fills the visible range"
    );
    await PlacesUtils.bookmarks.update({ guid: movedGuid, index: 2 });
    Assert.deepEqual(
      folderItems(popup).map(item => item.label),
      ["B", "C", "A"],
      "Moving to the last visible index replaces the boundary item"
    );
    let visibleItems = folderItems(popup);
    await PlacesUtils.bookmarks.remove(
      entries.find(entry => entry.title == "D").guid
    );
    Assert.deepEqual(
      folderItems(popup),
      visibleItems,
      "Removing a hidden bookmark leaves the visible elements in place"
    );
    Assert.ok(
      !popup.querySelector(MORE_SELECTOR),
      "Removing the last hidden bookmark removes the sidebar command"
    );
    await PlacesUtils.bookmarks.insert({
      parentGuid: entries[0].guid,
      index: 3,
      title: "New hidden bookmark",
      url: "https://example.com/visible-move/hidden",
    });
    Assert.deepEqual(
      folderItems(popup),
      visibleItems,
      "Inserting at the first hidden index leaves the visible elements in place"
    );
    Assert.ok(
      popup.querySelector(MORE_SELECTOR),
      "Inserting the first hidden bookmark adds the sidebar command"
    );
  } finally {
    view.uninit();
    menu.remove();
    await PlacesUtils.bookmarks.remove(entries[0].guid);
    await SpecialPowers.popPrefEnv();
  }
});
